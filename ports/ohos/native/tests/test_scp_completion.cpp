// SPDX-License-Identifier: BSD-3-Clause
#include "scp_completion.h"
#include <cassert>
#include <iostream>
#include <vector>
using teraterm::ohos::CompleteScpFile;
int main(){
  unsigned cases=0;
  for(bool upload:{true,false}){
    std::string order,error;
    assert(CompleteScpFile(upload,[&](char* b,bool write)->long{order+=write?'W':'R';assert(*b==0);*b=0;return 1;},[]{return false;},-37,&error));
    assert(order==(upload?"WR":"RW"));++cases;
  }
  {
    std::string error;unsigned calls=0;
    assert(!CompleteScpFile(false,[&](char* b,bool)->long{++calls;*b=1;return 1;},[]{return false;},-37,&error));
    assert(calls==1&&!error.empty());++cases;
  }
  for(long failure:{0L,-1L}){
    std::string error;
    assert(!CompleteScpFile(true,[&](char*,bool)->long{return failure;},[]{return false;},-37,&error));
    assert(!error.empty());++cases;
  }
  {
    std::string error;unsigned calls=0,waits=0;
    assert(CompleteScpFile(false,[&](char* b,bool)->long{++calls;if(calls==1||calls==3)return -37;*b=0;return 1;},[&]{++waits;return true;},-37,&error));
    assert(calls==4&&waits==2);++cases;
  }
  {
    std::string error="socket timeout";unsigned waits=0;
    assert(!CompleteScpFile(true,[](char*,bool)->long{return -37;},[&]{++waits;return false;},-37,&error));
    assert(waits==1&&error=="socket timeout");++cases;
  }
  {
    std::string error;unsigned calls=0;
    assert(!CompleteScpFile(true,[&](char* b,bool write)->long{++calls;if(!write)*b=2;return 1;},[]{return false;},-37,&error));
    assert(calls==2&&!error.empty());++cases;
  }
  assert(cases==8);std::cout<<"SCP completion: "<<cases<<" passed, 0 skipped\n";
}
