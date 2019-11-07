#ifndef _DAQCONTROLLER_HH_
#define _DAQCONTROLLER_HH_

#include <thread>
#include <atomic>
#include "V1724_MV.hh"
#include "DAXHelpers.hh"
#include "Options.hh"
#include "StraxInserter.hh"

struct processingThread{
  std::thread *pthread;
  StraxInserter *inserter;
};

class DAQController{
  /*
    Main control interface for the DAQ. Control scripts and
    user-facing interfaces can call this directly.
  */
  
public:
  DAQController(MongoLog *log=NULL, std::string hostname="DEFAULT");
  ~DAQController();

  int InitializeElectronics(Options *options, std::vector<int> &keys,
			    std::map<int, std::map<std::string, std::vector<double>>>&dac_values);

  int status(){
    return fStatus;
  };
  int buffer_length(){
    return fBufferLength;
  };
  std::string run_mode();    
  
  int Start();
  int Stop();
  void ReadData(int link);
  void End();

  int GetData(std::vector <data_packet> *&retVec);
    
  // Static wrapper so we can call ReadData in a std::thread
  void ReadThreadWrapper(void* data, int link);
  void ProcessingThreadWrapper(void* data);

  u_int64_t GetDataSize(){ u_int64_t ds = fDatasize; fDatasize=0; return ds;};
  std::map<int, u_int64_t> GetDataPerDigi();
  bool CheckErrors();
  int OpenProcessingThreads();
  void CloseProcessingThreads();

  std::map<std::string, int> GetDataFormat();
  
private:
  void AppendData(vector<data_packet> &d);
  void InitLink(vector<V1724*>&, std::map<int, std::map<std::string, std::vector<double>>>&, int&);
  
  std::vector <processingThread> fProcessingThreads;  
  std::map<int, std::vector <V1724*>> fDigitizers;
  std::mutex fBufferMutex;
  
  bool fReadLoop;
  std::vector<data_packet> *fRawDataBuffer;
  int fStatus;
  int fNProcessingThreads;
  string fHostname;
  MongoLog *fLog;
  Options *fOptions;
  
  // For reporting to frontend
  u_int64_t fBufferLength;
  u_int64_t fDatasize;
  std::map<int, std::atomic_ulong> fDataPerDigi;
  
  
};

#endif
