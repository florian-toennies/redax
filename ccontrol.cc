#include "CControl_Handler.hh"

int main(int argc, char** argv){
  
  mongocxx::instance instance{};
  // Parse arguments
  if(argc < 3){
    std::cout<<"USAGE: ./ccontrol {ID} {MONGO_URI}"<<std::endl;
    std::cout<<"Where MONGO_URI is the URI of the command DB"<<std::endl;
    return -1;
  }

  // Control and status DB connectivity
  // We're going to poll control for commands
  // and update status with a heartbeat
  std::string mongo_uri = argv[2];
  mongocxx::uri uri(mongo_uri.c_str());
  mongocxx::client client(uri);
  mongocxx::database db = client["dax"];
  mongocxx::collection control = db["control"];
  mongocxx::collection status = db["status"];
  mongocxx::collection col_options = db["options"];

  // Build a unique name for this process
  // we trust that the user has given {ID} uniquely
  char chostname[HOST_NAME_MAX];
  gethostname(chostname, HOST_NAME_MAX);
  std::string hostname = chostname;
  hostname += "_controller_";
  hostname += argv[1];
  std::cout<<"I dub thee "<<hostname<<std::endl;

  // Logging
  MongoLog *logger = new MongoLog();
  int ret = logger->Initialize(mongo_uri, "dax", "log", hostname, true);
  if(ret!=0){
    std::cout<<"Log couldn't be initialized. Exiting."<<std::endl;
    exit(-1);
  }

  // Holds session data
  CControl_Handler *fHandler = new CControl_Handler(logger, hostname);  

  while(1){
    mongocxx::cursor cursor = control.find
      (
       bsoncxx::builder::stream::document{}<< "host" << hostname <<"acknowledged" <<
       bsoncxx::builder::stream::open_document << "$ne" << hostname <<
       bsoncxx::builder::stream::close_document << bsoncxx::builder::stream::finalize
       );
    
    for (auto doc : cursor) {
      // Acknowledge the commands
      control.update_one
        (bsoncxx::builder::stream::document{} << "_id" << doc["_id"].get_oid() <<
         bsoncxx::builder::stream::finalize,
         bsoncxx::builder::stream::document{} << "$push" <<
         bsoncxx::builder::stream::open_document << "acknowledged" << hostname <<
         bsoncxx::builder::stream::close_document <<
         bsoncxx::builder::stream::finalize
         );
      
      // Strip data from the supplied doc
      int run = -1;
      std::string command = "";
      try{
	command = doc["command"].get_utf8().value.to_string();
	if(command == "arm" )
	  run = doc["number"].get_int32();
      }
      catch(const std::exception E){
        logger->Entry("ccontrol: Received a document from the dispatcher missing [command|number]",
		      MongoLog::Warning);
	continue;
      }
      
     // If the command is arm gonna use the options file to load the V2718, DDC10, etc...settings
     std::string mode = "";
     if(command == "arm"){
       try{
	 mode = doc["mode"].get_utf8().value.to_string();
       }
       catch(const std::exception E){
	 logger->Entry("ccontrol: Received an arm document with no run mode",MongoLog::Warning);
       }
                  
       // Now get the options in the same way as for initialising the digitisers, etc.. 
       bsoncxx::stdx::optional<bsoncxx::document::value> trydoc;
       try{
	 std::string option_name = doc["mode"].get_utf8().value.to_string();
	 trydoc = col_options.find_one(bsoncxx::builder::stream::document{}<<
				       "name" << option_name.c_str() <<
				       bsoncxx::builder::stream::finalize);
       }
       catch(const std::exception E){
	 logger->Entry("ccontrol: Received an improper options doc", MongoLog::Warning);
       }
            
       // Get an override doc from the 'options_override' field if it exists
       std::string override_json = "";
       try{
	 bsoncxx::document::view oopts = doc["options_override"].get_document().view();
	 override_json = bsoncxx::to_json(oopts);
       } 
       catch(const std::exception E){
	 logger->Entry("No override options provided", MongoLog::Debug);
       }	  
       
       // Get all the subdocs in options
       if(trydoc){
	 std::vector<std::string> include_json;
	 try{ 
             bsoncxx::array::view include_array = (*trydoc).view()["includes"].get_array().value;
	     for(bsoncxx::array::element ele : include_array){
               auto sd = col_options.find_one(bsoncxx::builder::stream::document{}<<
					      "name" << ele.get_utf8().value.to_string() <<
					      bsoncxx::builder::stream::finalize);
	       if(sd)
		 include_json.push_back(bsoncxx::to_json(*sd));
	       else
		 logger->Entry("ccontrol: Possible improper run config.", MongoLog::Warning);
	     }
	 }
	 catch(const std::exception E){ 
	   logger->Entry("Could not get all the subdocs in options doc", MongoLog::Debug);
	 }

	 //Here are our options
	 std::string options = bsoncxx::to_json(*trydoc);
	 
	 // Initialise the V2178, V1495 and DDC10...etc.      
	 if(fHandler->DeviceArm(run,options, include_json, override_json) != 0){
	   logger->Entry("Failed to initialize devices", MongoLog::Error);
	 }
	 
       } // end if "trydoc"
     } // end if "arm" command
     

    else if(command == "start"){
       if((fHandler->DeviceStart()) != 0){
	 logger->Entry("Failed to start devices", MongoLog::Debug);
       }
     } 
     else if(command == "stop"){
       if((fHandler->DeviceStop()) != 0){
	 logger->Entry("Failed to stop devices", MongoLog::Debug);
       }
     } 
    } //end for  
 
    // Report back on what we are doing
    status.insert_one(fHandler->GetStatusDoc(hostname));
   // Heartbeat and status info to monitor DB
   usleep(1000000);
  }
  return 0;
}