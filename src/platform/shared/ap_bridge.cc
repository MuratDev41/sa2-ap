#include "platform/shared/ap_bridge.h"
#include <asio.hpp>
#include <asio/strand.hpp>
#include <string>

// Clever macro to overload expires_from_now based on argument count
#define EXP_CHOOSER(_f1, _f2, _f3, ...) _f3
#define expires_from_now(...) EXP_CHOOSER(dummy, ##__VA_ARGS__, expires_after, expiry)(__VA_ARGS__)

namespace websocketpp {
namespace lib {
namespace asio {
    using namespace ::asio;

    // Overload is_neg to accept time_point
    template <typename Clock, typename Duration>
    bool is_neg(std::chrono::time_point<Clock, Duration> tp) {
        return tp < Clock::now();
    }

    // 1. steady_timer with expires_from_now() compat
    class steady_timer : public ::asio::basic_waitable_timer<std::chrono::steady_clock> {
    public:
        using basic_waitable_timer::basic_waitable_timer;
    };

    // 2. io_service and nested strand/work compat
    class io_service : public ::asio::io_context {
    public:
        using io_context::io_context;
        
        class strand : public ::asio::strand<io_context::executor_type> {
        public:
            strand(io_context& io) : ::asio::strand<io_context::executor_type>(io.get_executor()) {}
            
            template <typename Handler>
            auto wrap(Handler&& handler) {
                return ::asio::bind_executor(*this, std::forward<Handler>(handler));
            }
        };

        class work {
        private:
            ::asio::executor_work_guard<io_context::executor_type> guard_;
        public:
            work(io_context& io) : guard_(::asio::make_work_guard(io)) {}
        };

        template <typename Handler>
        void post(Handler&& handler) {
            ::asio::post(*this, std::forward<Handler>(handler));
        }
        
        void reset() {
            restart();
        }
    };

    // 3. socket_base max_connections compat
    class socket_base : public ::asio::socket_base {
    public:
        static const int max_connections = SOMAXCONN;
    };

    // 4. ip::tcp::resolver::iterator and query compat
    namespace ip {
        class tcp : public ::asio::ip::tcp {
        public:
            class resolver : public ::asio::ip::basic_resolver<::asio::ip::tcp> {
            public:
                using basic_resolver::basic_resolver;
                using iterator = results_type::iterator;
                
                struct query {
                    std::string host;
                    std::string service;
                    query(const std::string& h, const std::string& s) : host(h), service(s) {}
                    query(const std::string& h, int port) : host(h), service(std::to_string(port)) {}
                };
                
                iterator resolve(const query& q) {
                    return basic_resolver::resolve(q.host, q.service).begin();
                }
                
                iterator resolve(const query& q, ::asio::error_code& ec) {
                    auto res = basic_resolver::resolve(q.host, q.service, ec);
                    if (ec) return iterator();
                    return res.begin();
                }
                
                template <typename Handler>
                void async_resolve(const query& q, Handler&& handler) {
                    basic_resolver::async_resolve(q.host, q.service,
                        [h = std::forward<Handler>(handler)](const ::asio::error_code& ec, results_type results) mutable {
                            if (ec) {
                                h(ec, iterator());
                            } else {
                                h(ec, results.begin());
                            }
                        }
                    );
                }
            };
        };
    }

    // Intercept async_connect with 3 arguments (no connect condition)
    template <typename Protocol, typename Executor, typename Iterator, typename ConnectHandler>
    auto async_connect(::asio::basic_socket<Protocol, Executor>& s,
                       Iterator begin,
                       ConnectHandler&& handler) {
        return ::asio::async_connect(s, begin, Iterator(), 
                                     std::forward<ConnectHandler>(handler));
    }

    // Intercept async_connect with 4 arguments (with connect condition)
    template <typename Protocol, typename Executor, typename Iterator, typename ConnectCondition, typename ConnectHandler>
    auto async_connect(::asio::basic_socket<Protocol, Executor>& s,
                       Iterator begin,
                       ConnectCondition&& connect_condition,
                       ConnectHandler&& handler) {
        return ::asio::async_connect(s, begin, Iterator(), 
                                     std::forward<ConnectCondition>(connect_condition), 
                                     std::forward<ConnectHandler>(handler));
    }
}
}
}

// Support for other non-lib direct Asio references in global/wswrap
namespace asio {
    using io_service = websocketpp::lib::asio::io_service;
}

#include <apclient.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>
#include <chrono>
#include <mutex>

struct APNotification {
    std::string text;
    std::chrono::steady_clock::time_point timestamp;
    float duration;
};

static std::vector<APNotification> sNotifications;
static std::mutex sNotificationsMutex;

void AP_AddNotification(const std::string& text, float duration = 6.0f) {
    std::lock_guard<std::mutex> lock(sNotificationsMutex);
    APNotification notif;
    notif.text = text;
    notif.timestamp = std::chrono::steady_clock::now();
    notif.duration = duration;
    sNotifications.push_back(notif);
    
    // Cap notification list size
    if (sNotifications.size() > 50) {
        sNotifications.erase(sNotifications.begin());
    }
}

std::vector<APNotification> AP_GetActiveNotifications() {
    std::lock_guard<std::mutex> lock(sNotificationsMutex);
    std::vector<APNotification> active;
    auto now = std::chrono::steady_clock::now();
    for (const auto& notif : sNotifications) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - notif.timestamp).count() / 1000.0f;
        if (elapsed < notif.duration) {
            active.push_back(notif);
        }
    }
    return active;
}

bool AP_HasActiveNotifications() {
    std::lock_guard<std::mutex> lock(sNotificationsMutex);
    auto now = std::chrono::steady_clock::now();
    for (const auto& notif : sNotifications) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - notif.timestamp).count() / 1000.0f;
        if (elapsed < notif.duration) {
            return true;
        }
    }
    return false;
}

// Connection parameters bound to the ImGui menu
char gApServerIp[128] = "archipelago.gg:38281";
char gApSlotName[64] = "Player";
char gApPassword[64] = "";

// Status flags
bool8 gApIsConnected = 0;
char gApStatusMessage[256] = "Disconnected";

static std::unique_ptr<APClient> sApClient;

extern "C" {
#include "game/sa2/save.h"

static int sStartingCharacter = 0;
static int sStartingZone = 0;
static int sLockZones = 1;
static u8 sReceivedCharactersMask = 0;
static u8 sReceivedEmeraldsMask = 0;
static u8 sReceivedZoneMask = 0;

extern s8 gCurrentLevel;
extern u8 gGameMode;
extern void CreateCourseSelectionScreen(u8 currentLevel, u8 maxLevel, u8 cutScenes);
extern Task *gEntitiesManagerTask;
extern u8 gDemoPlayCounter;
extern u32 gFlags;
extern u8 gBgSpritesCount;
extern u8 gBackgroundsCopyQueueCursor;
extern u8 gBackgroundsCopyQueueIndex;
extern u8 gVramGraphicsCopyCursor;
extern u8 gVramGraphicsCopyQueueIndex;
extern void TasksDestroyInPriorityRange(u16, u16);

void AP_Init(void) {
    // Client is constructed dynamically upon AP_Connect()
}

void AP_Update(void) {
    if (sApClient) {
        try {
            sApClient->poll();
        } catch (const std::exception& e) {
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Poll Error: %s", e.what());
            gApIsConnected = 0;
        }
    }

    // Continuously enforce Archipelago-unlocked characters and emeralds in GBA RAM every frame!
    if (gApIsConnected && gLoadedSaveGame) {
        gLoadedSaveGame->unlockedCharacters = (1 << sStartingCharacter) | sReceivedCharactersMask;
        for (int c = 0; c < 5; c++) {
            gLoadedSaveGame->chaosEmeralds[c] = sReceivedEmeraldsMask;
        }

        // Continuously enforce zone locks in GBA RAM every frame!
        if (sLockZones) {
            int max_level = 2; // Default to Leaf Forest Boss (ZONE_1, ACT_BOSS)
            if (sReceivedZoneMask & (1 << 6)) { // XX
                max_level = 30;
            } else {
                int max_zone = sStartingZone;
                for (int z = 0; z < 7; z++) {
                    bool unlocked = (sStartingZone == z);
                    if (z == 0) {
                        unlocked = unlocked || (sReceivedZoneMask & (1 << 7));
                    } else {
                        unlocked = unlocked || (sReceivedZoneMask & (1 << (z - 1)));
                    }
                    if (unlocked && z > max_zone) {
                        max_zone = z;
                    }
                }
                
                max_level = (max_zone * 4) + 2; // LEVEL_INDEX(max_zone, ACT_BOSS)
            }

            for (int c = 0; c < 5; c++) {
                gLoadedSaveGame->unlockedLevels[c] = max_level;
            }

            // Strict Non-Contiguous Zone Locking enforcement:
            // Edge-triggered: only check once per level entry, not every frame.
            // We track the last level we saw while in a valid gameplay state.
            static s8 sLastSeenLevel = -1;
            bool in_gameplay = (gGameMode == 0 && gDemoPlayCounter == 0 && gEntitiesManagerTask != NULL);

            if (in_gameplay) {
                // Only evaluate on the frame the player first enters a new level
                if (gCurrentLevel != sLastSeenLevel) {
                    sLastSeenLevel = gCurrentLevel;
                    
                    int current_zone = gCurrentLevel / 4;
                    bool level_unlocked = false;
                    
                    if (gCurrentLevel == 30) {
                        level_unlocked = (sReceivedZoneMask & (1 << 6)) != 0; // True Area 53
                    } else if (current_zone == 7) {
                        level_unlocked = (sReceivedZoneMask & (1 << 6)) != 0; // XX Zone
                    } else if (current_zone >= 0 && current_zone <= 6) {
                        level_unlocked = (sStartingZone == current_zone);
                        if (current_zone == 0) {
                            level_unlocked = level_unlocked || (sReceivedZoneMask & (1 << 7)); // Leaf Forest
                        } else {
                            level_unlocked = level_unlocked || (sReceivedZoneMask & (1 << (current_zone - 1))); // Hot Crater to Egg Utopia
                        }
                    }

                    if (!level_unlocked) {
                        printf("[AP Native] Blocked entry to locked zone %d (level %d). Kicking to Course Select.\n", current_zone, gCurrentLevel);
                        sLastSeenLevel = -1; // Reset so the Course Select screen doesn't get re-caught
                        gFlags &= ~0x400; // Clear FLAGS_PAUSE_GAME
                        TasksDestroyInPriorityRange(0, 0xFFFF);
                        gBackgroundsCopyQueueCursor = gBackgroundsCopyQueueIndex;
                        gBgSpritesCount = 0;
                        gVramGraphicsCopyCursor = gVramGraphicsCopyQueueIndex;
                        CreateCourseSelectionScreen(0, max_level, 0);
                    }
                }
            } else {
                // Not in gameplay (menus, cutscenes etc) — keep tracking reset so 
                // the next level entry always triggers a fresh check.
                sLastSeenLevel = -1;
            }
        }
    }
}

void AP_Connect(void) {
    std::string ip = gApServerIp;
    std::string slot = gApSlotName;
    std::string password = gApPassword;

    gApIsConnected = 0;
    snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Connecting...");

    try {
        // Construct native APClient. The first argument is a UUID, second is the game name, third is server IP.
        sApClient = std::make_unique<APClient>("sa2_gba_native_uuid", "Sonic Advance 2", ip);

        sApClient->set_socket_connected_handler([slot, password]() {
            gApIsConnected = 0;
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Socket connected, logging in...");
            if (sApClient) {
                sApClient->ConnectSlot(slot, password, 7); // items_handling = 7 (all)
            }
        });

        sApClient->set_socket_error_handler([](const std::string& err) {
            gApIsConnected = 0;
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Network Error: %s", err.c_str());
        });

        sApClient->set_socket_disconnected_handler([]() {
            gApIsConnected = 0;
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Disconnected");
        });

        sApClient->set_print_handler([](const std::string& msg) {
            AP_AddNotification(msg);
        });

        sApClient->set_slot_connected_handler([](const nlohmann::json& slotData) {
            gApIsConnected = 1;
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Successfully Connected!");
            
            // Get starting character
            int starting_char = 0;
            if (slotData.contains("start_char")) {
                starting_char = slotData["start_char"].get<int>();
            } else if (slotData.contains("starting_character")) {
                starting_char = slotData["starting_character"].get<int>();
            }
            sStartingCharacter = starting_char;
            printf("[AP Native] Starting character from slot data: %d\n", starting_char);
            
            // Get starting zone
            int starting_zone = 0;
            if (slotData.contains("start_zone")) {
                starting_zone = slotData["start_zone"].get<int>();
            }
            sStartingZone = starting_zone;
            printf("[AP Native] Starting zone from slot data: %d\n", starting_zone);
            
            // Get lock_zones option
            int lock_zones = 1;
            if (slotData.contains("lock_zones")) {
                lock_zones = slotData["lock_zones"].get<int>();
            }
            sLockZones = lock_zones;
            printf("[AP Native] Lock zones from slot data: %d\n", lock_zones);

            // Unlock starting character!
            if (gLoadedSaveGame) {
                if (starting_char >= 0 && starting_char < 5) {
                    gLoadedSaveGame->unlockedCharacters |= (1 << starting_char);
                }
            }
        });

        sApClient->set_slot_refused_handler([](const std::list<std::string>& errors) {
            gApIsConnected = 0;
            std::string err_str = "Refused: ";
            for (const auto& err : errors) {
                err_str += err + " ";
            }
            if (err_str.length() > 255) {
                err_str = err_str.substr(0, 250) + "...";
            }
            snprintf(gApStatusMessage, sizeof(gApStatusMessage), "%s", err_str.c_str());
        });

        sApClient->set_items_received_handler([](const std::list<APClient::NetworkItem>& items) {
            u8 char_mask = 0;
            u8 emerald_mask = 0;
            u8 zone_mask = 0;
            
            for (const auto& item : items) {
                printf("[AP Native] Received item ID: %lld\n", (long long)item.item);
                
                // Unlock characters
                int char_idx = -1;
                if (item.item == 882500) char_idx = 1;      // Cream
                else if (item.item == 882501) char_idx = 2; // Tails
                else if (item.item == 882502) char_idx = 3; // Knuckles
                else if (item.item == 882503) char_idx = 4; // Amy
                else if (item.item == 882504) char_idx = 0; // Sonic
                
                if (char_idx != -1) {
                    char_mask |= (1 << char_idx);
                }
                
                // Unlock Chaos Emeralds (882510 to 882516)
                if (item.item >= 882510 && item.item <= 882516) {
                    int emerald_idx = item.item - 882510;
                    emerald_mask |= (1 << emerald_idx);
                }

                // Unlock Zones (882520 to 882527)
                if (item.item >= 882520 && item.item <= 882527) {
                    int zone_idx = item.item - 882520;
                    zone_mask |= (1 << zone_idx);
                }
            }
            
            sReceivedCharactersMask = char_mask;
            sReceivedEmeraldsMask = emerald_mask;
            sReceivedZoneMask = zone_mask;
            
            if (gLoadedSaveGame) {
                gLoadedSaveGame->unlockedCharacters = (1 << sStartingCharacter) | sReceivedCharactersMask;
                for (int c = 0; c < 5; c++) {
                    gLoadedSaveGame->chaosEmeralds[c] = sReceivedEmeraldsMask;
                }
            }
        });

        // Trigger connection (apclientpp will automatically connect socket first)
    } catch (const std::exception& e) {
        snprintf(gApStatusMessage, sizeof(gApStatusMessage), "Init Error: %s", e.what());
        gApIsConnected = 0;
        sApClient.reset();
    }
}

static int64_t get_ap_location_id(u8 zone, u8 act, bool8 isBoss) {
    int64_t sa2_base_id = 882000;
    int64_t character = gSelectedCharacter;
    if (character < 0 || character >= 5) {
        character = 0; // Default to Sonic
    }
    int64_t offset = (character * 30) + (zone * 3) + (isBoss ? 2 : act);
    return sa2_base_id + offset;
}

void AP_SendLocationCheck(u8 zone, u8 act, bool8 isBoss) {
    if (sApClient && gApIsConnected) {
        try {
            int64_t loc_id = get_ap_location_id(zone, act, isBoss);
            std::list<int64_t> checks = {loc_id};
            sApClient->LocationChecks(checks);
            printf("[AP Native] Sent check for Char %d Zone %d Act %d Boss %d -> ID %lld\n", gSelectedCharacter, zone, act, isBoss, (long long)loc_id);
        } catch (const std::exception& e) {
            printf("[AP Native] Error sending check: %s\n", e.what());
        }
    }
}

void AP_SendChaosEmeraldCheck(u8 emeraldId) {
    if (sApClient && gApIsConnected) {
        try {
            int64_t loc_id = 882000 + 200 + emeraldId;
            std::list<int64_t> checks = {loc_id};
            sApClient->LocationChecks(checks);
            printf("[AP Native] Sent Emerald check: ID %lld\n", (long long)loc_id);
        } catch (const std::exception& e) {
            printf("[AP Native] Error sending emerald: %s\n", e.what());
        }
    }
}

} // extern "C"
