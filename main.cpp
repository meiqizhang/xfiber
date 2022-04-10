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

    xfiber->CreateFiber([&]{
        Listener listener = Listener::ListenTCP(6379);
        while (true) {
            shared_ptr<Connection> conn = listener.Accept();
            xfiber->CreateFiber([conn] {
                while (true) {
                    char recv_buf[512];
                    int n = conn->Read(recv_buf, 512);
                    if (n <= 0) {
                        break;
                    }
                    recv_buf[n] = '\0';
                    //cout << "recv: " << recv_buf << endl;
                    char *rsp = "+OK\r\n";
                    conn->Write(rsp, strlen(rsp));
                }
            }, 0, "server");
        }
    });
    
    xfiber->Dispatch();

    return 0;
}
