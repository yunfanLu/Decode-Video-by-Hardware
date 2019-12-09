//#include "hw_decode.hpp"
#include <string>
#include <iostream>
#include <unistd.h>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << argv[0] << " filename" << endl;
        return -1;
    }       
    const char* dt = "cuda";
    HWDecoder vid_decoder(dt);
    // cout<<"Sleeping 2s"<<endl;
    // sleep(2);
    
    const char* filename = argv[1];
    
    int num_gops = 0;

    int ret = vid_decoder.get_num_gops(filename, &num_gops);
    if (ret < 0)
        cout << "get num gops failed" << endl;
    else
        cout << "num_gops: " << num_gops << endl;

    for (int gop_target=0; gop_target<num_gops; ++gop_target) {
        ret = vid_decoder.get_gop_frame(filename, gop_target);
        if (ret < 0)
            cout << "get gop frame failed" << endl;
    }    
    return 0;
        
}
