#include <uwebsockets/App.h>
#include <iostream>

int main(){
    uWS::App().get("/", [](auto *res, auto *req) {
        res->end("Hello uWebSockets!");
    }).listen(8080, [](auto *socket){
        if(socket){
            std::cout << "Servidor rodando em http://localhost:8080\n";
        }
    }).run();
    
    return 0;
}
