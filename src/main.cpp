#include <uwebsockets/App.h>
#include <iostream>
#include <string>

int main(){

    uWS::App app;

    app.get("/ready", [](auto *res, auto *req) {
        res->writeStatus("200 OK");
        res->writeHeader("content-type", "application/json");
        res->end(R"({"status":"ready"})");
    });
    
    app.post("/fraud-score", [](auto *res, auto *req){
        std::string body;

        res->onAborted([]() {});

        res->onData([res, body = std::move(body)](std::string_view chunk, bool isLast) mutable {
            body.append(chunk.data(), chunk.size());

            if (isLast){
                std::cout << "Payload recebido:\n" << body << "\n";
                res->writeStatus("200 OK");
                res->writeHeader("content-type", "application/json");
                res->end(R"({"approved":true,"fraud_score":0.0})");
            }
        });
    });

    app.listen(8080, [](auto *socket){
        if(socket){
            std::cout << "Servidor rodando em http://localhost:8080\n";
        }
    });

    app.run();
    
    return 0;
}
