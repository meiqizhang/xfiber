#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>

#include "xfiber.h"
#include "xsocket.h"

using namespace std;


void sigint_action(int sig) {
    std::cout << "exit..." << std::endl;
    exit(0);    
}

// #define DEBUG_ENABLE
#define DPRINT(fmt, args...) fprintf(stderr, "[D][%s %d] " fmt"\n", __FILE__, __LINE__, ##args);


int main() {
    signal(SIGINT, sigint_action);

    XFiber *xfiber = XFiber::xfiber();
    /*xfiber->AddTask([&]() {
        cout << "hello world 11" << endl;
        xfiber->Yield();
        cout << "hello world 12" << endl;
         xfiber->Yield();
        cout << "hello world 13" << endl;
        xfiber->Yield();
        cout << "hello world 14" << endl;
        cout << "hello world 15" << endl;

    }, 0, "f1");

    xfiber->AddTask([]() {
        cout << "hello world 2" << endl;
    }, 0, "f2");
    */

    // xfiber->CreateFiber([xfiber]{
    //     for (int i = 0; i < 10; i++) {
    //         cout << i << endl;
    //         xfiber->SleepMs(1000);
    //     }
    // });

    xfiber->CreateFiber([&]{
        Listener listener = Listener::ListenTCP(7000);
        while (true) {
            shared_ptr<Connection> conn1 = listener.Accept();
            //shared_ptr<Connection> conn2 = Connection::ConnectTCP("127.0.0.1", 6379);

            xfiber->CreateFiber([conn1] {
                while (true) {
                    char recv_buf[512];
                    int n = conn1->Read(recv_buf, 512, 50000);
                    if (n <= 0) {
                        break;
                    }

                 #if 0
                    conn2->Write(recv_buf, n);
                    char rsp[1024];
                    int rsp_len = conn2->Read(rsp, 1024);
                    cout << "recv from remote: " << rsp << endl;
                    conn1->Write(rsp, rsp_len);
                #else
                    if (conn1->Write("+OK\r\n", 5, 1000) <= 0) {
                        break;
                    }
                #endif
                }
            }, 0, "server");
        }
    });
    
    xfiber->Dispatch();

    return 0;
}
