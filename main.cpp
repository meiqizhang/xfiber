#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <cstring>
#include <memory>

#include "xfiber.h"
#include "listener.h"
#include "conn.h"

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

    xfiber->AddTask([&]{
        Listener listener = Listener::ListenTCP(8888);
        while (true) {
            shared_ptr<Connection> conn = listener.Accept();
            xfiber->AddTask([conn] {
                while (true) {
                    string msg;
                    int n = conn->Read(msg);
                    if (n == 0) {
                        break;
                    }
                    cout << "recv: " << msg << endl;
                    msg = "echo " + msg;
                    while (msg.length() < 120157) {
                        msg += "+";
                    }
                    msg += "最后的字符";
                    conn->Write(msg);
                }
            }, 0, "server");
        }
    });
    
    xfiber->Dispatch();

    return 0;
}
