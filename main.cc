#include <iostream>
#include <string>
#include <iomanip>
#include <csignal>
#include "V1724.hh"
#include "DAQController.hh"

bool b_run = true;

void SignalHandler(int signum) {
    std::cout << "Received signal "<<signum<<std::endl;
    b_run = false;
    return;
}

int main(int argc, char** argv){

    // varibale that controls
    //  * wheter the main loop shall be exited after one loop
    //  * no acknowledgement happens at begining
    //       (removes need to start new run all the time when starting daq)
    
    bool flo_develop = true;
    
    
    std::cout << 
            std::endl <<
            std::endl <<
            std::endl <<
            std::endl;
    if(flo_develop){
        
         std::cout << 
            std::endl <<
            "\e[31m" <<
            "========================" <<
            std::endl <<
            "   WARNING: " <<
            std::endl <<
            "     in developer mode" <<
            std::endl <<
            "========================" <<
            "\e[0m" <<
            std::endl;
        
    }
    
  // Need to create a mongocxx instance and it must exist for
  // the entirety of the program. So here seems good.
  mongocxx::instance instance{};

  signal(SIGINT, SignalHandler);
  signal(SIGTERM, SignalHandler);
   
  std::string current_run_id="none";
  
  // Accept at least 2 arguments
  if(argc<3){
    std::cout<<"Welcome to DAX. Run with a unique ID and a valid mongodb URI"<<std::endl;
    std::cout<<"e.g. ./dax ID mongodb://user:pass@host:port/authDB"<<std::endl;
    std::cout<<"...exiting"<<std::endl;
    exit(0);
  } else {
    std::cout <<
            "\e[32m" <<
            "========================" <<
            std::endl <<
            "     Welcome to DAX.     " <<
            std::endl <<
            "       (for XEBRA)       " <<
            std::endl <<
            "========================" <<
            "\e[0m" <<
            std::endl <<
            std::endl <<
            std::endl <<
            std::endl;
  }
  
  string dbname = "xenonnt";
  if(argc >= 4)
    dbname = argv[3];

  // We will consider commands addressed to this PC's ID 
  char chostname[HOST_NAME_MAX];
  gethostname(chostname, HOST_NAME_MAX);
  std::string hostname=chostname;
  hostname+= "_reader_";
  string sid = argv[1];
  hostname += sid;
  std::cout<<"Reader starting with ID: "<<hostname<<std::endl;
  
  // MongoDB Connectivity for control database. Bonus for later:
  // exception wrap the URI parsing and client connection steps
  string suri = argv[2];  
  mongocxx::uri uri(suri.c_str());
  mongocxx::client client(uri);
  mongocxx::database db = client[dbname];
  mongocxx::collection control = db["control"];
  mongocxx::collection status = db["status"];
  mongocxx::collection options_collection = db["options"];
  
  // Logging
  MongoLog *logger = new MongoLog(true);
  int ret = logger->Initialize(suri, dbname, "log", hostname,
            "dac_values", true);
  if(ret!=0){
    std::cout<<"Exiting"<<std::endl;
    exit(-1);
  }
  
  //Options
  Options *fOptions = NULL;
  
  // The DAQController object is responsible for passing commands to the
  // boards and tracking the status
  DAQController *controller = new DAQController(logger, hostname);  
  std::vector<std::thread*> readoutThreads;
  
  // Main program loop. Scan the database and look for commands addressed
  // to this hostname. 
  while(b_run){
    
    // Try to poll for commands
    bsoncxx::stdx::optional<bsoncxx::document::value> querydoc;
    try{
      //   mongocxx::cursor cursor = control.find 
      querydoc = control.find_one
    (
        bsoncxx::builder::stream::document{} << "host" << hostname << "acknowledged" <<
        bsoncxx::builder::stream::open_document << "$ne" << hostname <<       
        bsoncxx::builder::stream::close_document << 
        bsoncxx::builder::stream::finalize
    );
    
    }catch(const std::exception &e){
      std::cout<<e.what()<<std::endl;
      std::cout<<"Can't connect to DB so will continue what I'm doing"<<std::endl;
    }
    
    
    //for(auto doc : cursor) {
    if(querydoc){
        auto doc = querydoc->view();
        
        std::cout << "\e[32mFound a doc\e[0m" << std::endl <<
            "\e[33m  command:\e[0m " << doc["command"].get_utf8().value.to_string() << std::endl <<
            "\e[33m  current status:\e[0m " << controller->status() << std::endl <<
            "";
        
        // Very first thing: acknowledge we've seen the command. If the command
        // fails then we still acknowledge it because we tried
        
        // disable updateing of document when developing
        if(!flo_develop){
            control.update_one
                (
                 bsoncxx::builder::stream::document{} << "_id" << (doc)["_id"].get_oid() <<
                 bsoncxx::builder::stream::finalize,
                 bsoncxx::builder::stream::document{} << "$push" <<
                 bsoncxx::builder::stream::open_document << "acknowledged" << hostname <<
                 bsoncxx::builder::stream::close_document <<
                 bsoncxx::builder::stream::finalize
                 );
            std::cout<<"Updated doc"<<std::endl;
        }
        // Get the command out of the doc
        string command = "";
        string user = "";
        string mode = "";
        try{
            command = (doc)["command"].get_utf8().value.to_string();
            user    = (doc)["user"].get_utf8().value.to_string();
            mode    = (doc)["mode"].get_utf8().value.to_string();
            
            
            
        
        } catch (const std::exception &e){
            //LOG
            logger->Entry(MongoLog::Warning, "Received malformed command %s",
                bsoncxx::to_json(doc).c_str());
        }
        
        // Process commands
        if(command == "start"){
        
            if(controller->status() == 2) {
            
                if(controller->Start()!=0){
                continue;
                }
                
                // Nested tried cause of nice C++ typing
                try{
                    current_run_id = (doc)["run_identifier"].get_utf8().value.to_string();	    
                }
                catch(const std::exception &e){
                    try{
                        current_run_id = std::to_string((doc)["run_identifier"].get_int32());
                    }
                    catch(const std::exception &e){
                        current_run_id = "na";
                    }
                }
                
                logger->Entry(MongoLog::Message, "Received start command from user %s",
                    user.c_str());
            } else{
                logger->Entry(MongoLog::Debug, "Cannot start DAQ since not in ARMED state");
            }
        } else if(command == "stop"){
            // "stop" is also a general reset command and can be called any time
            logger->Entry(MongoLog::Message, "Received stop command from user %s",
            user.c_str());
            if(controller->Stop()!=0){
                logger->Entry(MongoLog::Error,
                    "DAQ failed to stop. Will continue clearing program memory.");
            }
            current_run_id = "none";
            if(readoutThreads.size()!=0){
                for(auto t : readoutThreads){
                    t->join();
                    delete t;
                }
                readoutThreads.clear();	
            }
            controller->End();
        } else if(command == "arm"){
            
            // Can only arm if we're in the idle, arming, or armed state
            if(controller->status() == 0 || controller->status() == 1 || controller->status() == 2){
                std::cout<<"\e[32mSystem has valid status\e[0m"<<std::endl;
                
                // Join readout threads if they still are out there
                controller->Stop();
                if(readoutThreads.size() !=0){
                    for(auto t : readoutThreads){
                        std::cout<<"Joining orphaned readout thread"<<std::endl;
                        t->join();
                        delete t;
                    }
                    readoutThreads.clear();
                }
                
                // Clear up any previously failed things
                if(controller->status() != 0){
                    std::cout<<"\e[31mClear up any previously failed things\e[0m"<<std::endl;
                    controller->End();
                }
                
                // Get an override doc from the 'options_override' field if it exists
                std::string override_json = "";
                try{
                    bsoncxx::document::view oopts = (doc)["options_override"].get_document().view();
                    override_json = bsoncxx::to_json(oopts);
                }
                catch(const std::exception &e){
                    logger->Entry(MongoLog::Debug, "No override options provided, continue without.");
                }
                std::cout<<"\e[33m  overide options:\e[0m " << override_json << std::endl;
                
                bool initialized = false;
                
                // Mongocxx types confusing so passing json strings around
                if(fOptions != NULL){
                    delete fOptions;
                }
                fOptions = new Options(logger, (doc)["mode"].get_utf8().value.to_string(),
                    options_collection, override_json);
                
                std::vector<int> links;
                std::map<int, std::vector<u_int16_t>> written_dacs;
                
                // including includes is missing
                std::cout<<"\e[33m  mode: \e[0m " << mode << std::endl;
                
                
                if(controller->InitializeElectronics(fOptions, links, written_dacs) != 0){
                    logger->Entry(MongoLog::Error, "Failed to initialize electronics");
                    controller->End();
                } else {
                    logger->UpdateDACDatabase(fOptions->GetString("run_identifier", "default"),
                        written_dacs);
                    initialized = true;
                    logger->Entry(MongoLog::Debug, "Initialized electronics");
                }
                
                if(readoutThreads.size()!=0){
                    logger->Entry(MongoLog::Message,
                        "Cannot start DAQ while readout thread from previous run active. Please perform a reset");
                } else if(!initialized){
                    cout<<"Skipping readout configuration since init failed"<<std::endl;
                } else {
                    controller->CloseProcessingThreads();
                    for(unsigned int i=0; i<links.size(); i++){
                        std::cout<<"Starting readout thread for link "<<links[i]<<std::endl;
                        controller->OpenProcessingThreads(); // open nprocessingthreads per link
                        std:: thread *readoutThread = new std::thread
                            (
                                DAQController::ReadThreadWrapper,
                                (static_cast<void*>(controller)), links[i]
                            );
                        readoutThreads.push_back(readoutThread);
                    }
                }
            } else {
                logger->Entry(MongoLog::Warning, "Cannot arm DAQ while not 'Idle'");
                std::cout<<"\e[31mSystem has invalid status\e[0m"<<std::endl;
            }
        }
    }
    // Insert some information on this readout node back to the monitor DB
    controller->CheckErrors();

    try{

      // Put in status update document
      auto insert_doc = bsoncxx::builder::stream::document{};
      insert_doc << "host" << hostname <<
	"rate" << controller->GetDataSize()/1e6 <<
	"status" << controller->status() <<
	"buffer_length" << controller->buffer_length()/1e6 <<
	"run_mode" << controller->run_mode() <<
	"current_run_id" << current_run_id <<
	"boards" << bsoncxx::builder::stream::open_document <<
	[&](bsoncxx::builder::stream::key_context<> doc){
	for( auto const& kPair : controller->GetDataPerDigi() )	  
	  doc << std::to_string(kPair.first) << kPair.second/1e6;
	} << bsoncxx::builder::stream::close_document;
	status.insert_one(insert_doc << bsoncxx::builder::stream::finalize);
    }catch(const std::exception &e){
      std::cout<<"Can't connect to DB to update."<<std::endl;
      std::cout<<e.what()<<std::endl;
    }
    usleep(1000000);
    
    if(flo_develop){
        std::cout << std::endl << "\e[32quitting developer loop\e[0m" << std::endl;
        exit(0);
    }
  
  }
  delete logger;
  exit(0);
  

}



