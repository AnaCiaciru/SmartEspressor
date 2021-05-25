#include <iostream>
#include <typeinfo>

#include <pistache/net.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/http_headers.h>
#include <pistache/cookie.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>
#include <pistache/common.h>

#include <signal.h>

using namespace std;
using namespace Pistache;

// This is just a helper function to preety-print the Cookies that one of the enpoints shall receive.
void printCookies(const Http::Request &req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto &c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic {
    // practic dirijeaza pornirea serverului
    void handleReady(const Rest::Request &, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "1");
    }

}

class EspressorEndPoint {

public:
    explicit EspressorEndPoint(Address address)
            : httpEndpoint(std::make_shared<Http::Endpoint>(address)) {}

    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options().threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    void stop() {
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&EspressorEndPoint::doAuth, this));

        /// settings/sugar/4
        /// settings/size/small(medium, large)
        /// settings/aroma(caramel,coconut, vanilla,rum,none)

        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&EspressorEndPoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&EspressorEndPoint::getSetting, this));

        /// settings/type/americano(cappuccino,latte_machiato,mocha,espresso,none)
        Routes::Post(router, "/type/:typeName/", Routes::bind(&EspressorEndPoint::setType, this));
        Routes::Get(router, "/type/", Routes::bind(&EspressorEndPoint::getType, this));

        /// Un mod pentru curățarea automată a aparatului
        /// clean/all (sugar/size/aroma/type) -> merge doar ptr sugar
        Routes::Get(router, "/clean/:value", Routes::bind(&EspressorEndPoint::doClean, this));

    }

    void doAuth(const Rest::Request &request, Http::ResponseWriter response) {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies().add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok, "Auth is Done!");
    }

    void doClean(const Rest::Request &request, Http::ResponseWriter response) {
        /// practic vom pune toate valorile pe null

        Guard guard(espressorLock);

        /// al doilea parametru: all
        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<std::string>();
        }

        if (val == "all") {
            espr.cleanAll();
        }
        if (val == "sugar") {
            espr.cleanSugar();
        }
        if (val == "size")
            espr.cleanSize();
        if (val == "type")
            espr.cleanType();

        if (val == "aroma")
            espr.cleanAroma();

        response.send(Http::Code::Ok, "Clean is Done!");
    }

    /// settings
    void setSetting(const Rest::Request &request, Http::ResponseWriter response) {
        /// sugar/size/type/aroma
        auto settingName = request.param(":settingName").as<std::string>();

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(espressorLock);

        /// al doilea parametru: cantitatea sau dimensiunea (numar)
        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<std::string>();
        }

        // Setting the espressor's setting to value
        int setResponse = espr.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " was set to " + val);
        } else if (setResponse == 0) {
            response.send(Http::Code::Not_Found,
                          settingName + " was not found and or '" + val + "' was not a valid value ");
        } else if (setResponse == -1) {
            response.send(Http::Code::Not_Found,
                          "Te rog seteaza mai intai marimea paharului cu settings/size!");
        } else {
            response.send(Http::Code::Not_Found,
                          "Te rog pune atat zahar cat de mare ti-e paharul! small - max 2, medium - max 3, large - max 5!");
        }
    }

    // Endpoint to get one of the Espressor's settings.
    void getSetting(const Rest::Request &request, Http::ResponseWriter response) {
        /// sugar/size/type/aroma
        auto settingName = request.param(":settingName").as<std::string>();

        Guard guard(espressorLock);
        /// luam valoarea
        string valueSetting = espr.get(settingName);

        if (valueSetting != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                    .add<Header::Server>("pistache/0.1")
                    .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting);
        } else {
            response.send(Http::Code::Not_Found, settingName + " was not found");
        }

    }

    /// avem doar typeName ca parametru
    void setType(const Rest::Request &request, Http::ResponseWriter response) {
        auto typeName = request.param(":typeName").as<std::string>();

        Guard guard(espressorLock);
        int value;

        if (typeName == "") {
            response.send(Http::Code::Not_Found, "type is not valid!");
            return;
        }

        int setResponse = espr.setType(typeName);

        if (setResponse == 0) {
            response.send(Http::Code::Not_Found, "value is not valid!");
        } else {
            response.send(Http::Code::Ok, typeName + " was set!");
        }
    }

    void getType(const Rest::Request &request, Http::ResponseWriter response) {
        Guard guard(espressorLock);
        string getResponse = espr.getType();

        if (getResponse == "") {
            response.send(Http::Code::Not_Found, "coffee type is not valid!");
            return;
        } else {
            response.send(Http::Code::Ok, "Selected coffee: " + getResponse);
        }

    }

    class Espressor {
    public:
        explicit Espressor() {}

        int set(std::string name, std::string val) {

            if (name == "size") {
                sizeSetting.name = name;

                if (val == "small") {
                    sizeSetting.value = 1;
                    return 1;
                }

                if (val == "medium") {
                    sizeSetting.value = 2;
                    return 1;
                }

                if (val == "large") {
                    sizeSetting.value = 3;
                    return 1;
                }
            }

            if (name == "sugar") {
                /// nu poate seta zaharul daca nu este aleasa marimea paharului
                if (sizeSetting.name != "small" || sizeSetting.name != "medium" || sizeSetting.name != "large")
                    return -1;

                int value = std::stoi(val);
                sugarSetting.name = name;

                /// putem pune intre 1 si 5 pachetele de zahar
                if (value >= 1 && value < 5) {
                    /// in functie de marimea paharului hotaram cate pliculete de zahar poate pune

                    /// small
                    if (sizeSetting.name == "small") {
                        if (value > 2)
                            return -2;
                    }
                    /// medium
                    if (sizeSetting.name == "medium") {
                        if (value > 3)
                            return -2;
                    }
                    /// large - poate sa puna cat vrea in limita disponibila

                    sugarSetting.value = value;
                    return 1;
                }
            }

            /// caramel/coconut/vanilla/cacao/rum/none)
            if (name == "aroma") {
                aromaSetting.name = name;

                string possibleValues[6] = {"caramel", "coconut", "vanilla", "cacao", "rum", "none"};
                for (int i = 0; i < 6; i++)
                    if (val == possibleValues[i]) {
                        aromaSetting.value = i + 1;
                        return 1;
                    }
            }

            return 0;

        }

        string get(std::string name) {
            if (name == "sugar")
                return std::to_string(sugarSetting.value);
            if (name == "size") {
                int size_value = sizeSetting.value;
                if (size_value == 1)
                    return "small";
                if (size_value == 2)
                    return "medium";
                if (size_value == 3)
                    return "large";
            }

            if (name == "type")
                return getType();

            if (name == "aroma") {
                int aroma_value = aromaSetting.value;
                string possibleValues[6] = {"caramel", "coconut", "vanilla", "cacao", "rum", "none"};
                return possibleValues[aroma_value - 1];
            }
            return "";
        }


        /// functii ajutatoare pentru type
        int setType(std::string typeName) {
            // espresso, americano, cappuccino, latte_machiato, mocha
            if (typeName == "espresso") {
                coffeeType = espresso;
                return 1;
            }

            if (typeName == "americano") {
                coffeeType = americano;
                return 1;
            }

            if (typeName == "cappuccino") {
                coffeeType = cappuccino;
                return 1;
            }

            if (typeName == "latte_machiato") {
                coffeeType = latte_machiato;
                return 1;
            }

            if (typeName == "mocha") {
                coffeeType = mocha;
                return 1;
            }
            return 0;
        }

        string getType() {
            switch (coffeeType) {
                case espresso:
                    return "espresso";
                case americano:
                    return "americano";
                case cappuccino:
                    return "cappuccino";
                case latte_machiato:
                    return "latte_machiato";
                case mocha:
                    return "mocha";
                default:
                    return "";
            }
        }

        void cleanSugar() {
            sugarSetting.name = "Blank";
            sugarSetting.value = 0;
        }

        void cleanSize() {
            sizeSetting.name = "Blank";
            sizeSetting.value = 0;
        }

        void cleanType() {
            coffeeType = none;
        }

        void cleanAroma() {
            aromaSetting.name = "Blank";
            aromaSetting.value = 6; //sets to none
        }

        void cleanAll() {
            /// sugar
            cleanSugar();

            /// size
            cleanSize();

            /// aroma
            cleanAroma();

            /// type
            cleanType();
        }

        void setSizeInitial() {
            sizeSetting.name = "Blank";
            sizeSetting.value = 0;
        }

        int getSize() {
            return sizeSetting.value;
        }

    private:
        /*consideram deocamdata ca esspressorul are trei setari:
            - setarea pentru nivelul de zahar: sugarSetting, cu valori intregi in intervalul [1, 5]
            - setarea pentru marimea cafelei: sizeSetting, cu valori small, medium, large
            -setarea tipului de cafea: [americano, cappuccino, latte machiato, mocha, espresso]
        */
        /// definirea setarilor
        struct setting {
            std::string name;
            int value;
        } sugarSetting, sizeSetting, aromaSetting;

        enum type {
            americano = 1, cappuccino = 2, latte_machiato = 3, mocha = 4, espresso = 5, none = 6
        } coffeeType;

    };

    /// blocam celelalte optiuni
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock espressorLock;

    // Instance of the microwave model
    Espressor espr;

    std::shared_ptr <Http::Endpoint> httpEndpoint;
    Rest::Router router;
};


int main(int argc, char *argv[]) {
    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0
        || sigaddset(&signals, SIGTERM) != 0
        || sigaddset(&signals, SIGINT) != 0
        || sigaddset(&signals, SIGHUP) != 0
        || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    // Instance of the class that defines what the server can do.
    EspressorEndPoint stats(addr);

    // Initialize and start the server
    stats.init(thr);
    stats.start();


    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0) {
        std::cout << "received signal " << signal << std::endl;
    } else {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}