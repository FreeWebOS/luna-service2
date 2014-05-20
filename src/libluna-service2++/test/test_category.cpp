// @@@LICENSE
//
//      Copyright (c) 2014 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// LICENSE@@@

#include <pbnjson.hpp>

#include "luna-service2/lunaservice.hpp"
#include "luna-service2/lunaservice-meta.h"

#include <gtest/gtest.h>
#include "util.hpp"

using namespace std;
using pbnjson::JValue;
using pbnjson::JSchemaFragment;

class TestCategory
    : public ::testing::Test
{
protected:
    LS::Service sh, sh_client;
    GMainLoop *main_loop;

private:
    bool done; // still waiting

    virtual void SetUp()
    {
        done = false;
        ping = [&](LSMessage *) { finish(); return false; };
        pong = [&](LSMessage *) { finish(); return false; };

        main_loop = g_main_loop_new(nullptr, false);

        ASSERT_NO_THROW({ sh = LS::registerService("com.palm.test"); });
        ASSERT_NO_THROW({ sh.attachToLoop(main_loop); });

        ASSERT_NO_THROW({ sh_client = LS::registerService(); });
        ASSERT_NO_THROW({ sh_client.attachToLoop(main_loop); });
    }

    virtual void TearDown()
    {
        ASSERT_NO_THROW({ sh = LS::Service(); });
        ASSERT_NO_THROW({ sh_client = LS::Service(); });
        g_main_loop_unref(main_loop);
    }

protected:
    function<bool(LSMessage *message)> ping, pong;

    template<bool (TestCategory::*M)(LSMessage&)>
    static constexpr LSFilterFunc wrap()
    { return &LS::Service::methodWraper<TestCategory, M>; }

    void finish(bool done = true)
    { this->done = done; }

    void wait()
    {
        auto ctx = g_main_loop_get_context(main_loop);

        while (!done)
        { (void) g_main_context_iteration(ctx, true); }
        finish(false);
    }

    // specially for LS_CATEGORY_END forward registerCategory and
    // setCategoryData
    template <typename... Args>
    void registerCategory(Args &&... args)
    { sh.registerCategory(std::forward<Args>(args)...); }

    template <typename... Args>
    void setCategoryData(Args &&... args)
    { sh.setCategoryData(std::forward<Args>(args)...); }

public:
    bool cbPing(LSMessage &message) { return ping(&message); }
    bool cbPong(LSMessage &message) { return pong(&message); }
};

TEST_F(TestCategory, DummyRegister)
{
    ASSERT_NO_THROW({ sh.registerCategory("/", nullptr, nullptr, nullptr); });
}

TEST_F(TestCategory, UnregisteredSet)
{
    EXPECT_THROW({ sh.setCategoryData("/", this); }, LS::Error);
    EXPECT_THROW({ sh.setCategoryDescription("/", jnull()); }, LS::Error);
}

TEST_F(TestCategory, SetDescription)
{
    JRef description {
        { "methods", {
            { "ping", {
                { "type", "object" },
                { "description", "simple ping" },
                { "additionalProperties", false },
            }},
        }},
    };

    ASSERT_NO_THROW({ sh.registerCategory("/", nullptr, nullptr, nullptr); });
    EXPECT_NO_THROW({ sh.setCategoryDescription("/", description.get()); });

    // Actually we should to check that effect of prev setting disappeared
    // without any leaks. But at least we'll test that it doesn't fall.
    JRef description2 {
        { "methods", {
            { "ping", {
                { "type", "object" },
                { "description", "simple ping" },
                { "additionalProperties", false },
            }},
            { "pong", {
                { "type", "object" },
                { "description", "simple ping" },
                { "additionalProperties", false },
            }},
        }},
    };
    EXPECT_NO_THROW({ sh.setCategoryDescription("/", description2.get()); });
}

TEST_F(TestCategory, RegisterByMacro)
{
    // by some reason can't put it directly into ASSERT_NO_THROW
    auto reg = [&]() {
        LS_CATEGORY_BEGIN(TestCategory, /)
            LS_CATEGORY_METHOD(cbPing)
        LS_CATEGORY_END
    };
    ASSERT_NO_THROW({ reg(); });

    bool havePing = false, havePong = false;
    ping = [&](LSMessage *m) {
        havePing = true;
        LS::Error e;
        EXPECT_TRUE(LSMessageRespond(m, "{\"returnValue\":true, \"answer\":42}", e.get())) << e.what();
        return true;
    };
    pong = [&](LSMessage *m) {
        finish();
        havePong = true;
        auto response = fromJson(LSMessageGetPayload(m));
        EXPECT_EQ(JValue(true), response["returnValue"]);
        return true;
    };

    LS::Error e;
    LSMessageToken token;
    EXPECT_TRUE(
        LSCallOneReply(
            sh_client.get(), "luna://com.palm.test/cbPing", "{}",
            wrap<&TestCategory::cbPong>(), this, &token,
            e.get()
        )
    ) << e.what();
    wait();
    EXPECT_TRUE(havePing);
    EXPECT_TRUE(havePong);
}

TEST_F(TestCategory, BasicScenario)
{
    LS::Error e;
    LSMessageToken token;
    bool havePong = false, havePing = false;

    // call to bare service (even without categories)
    pong = [&](LSMessage *m) {
        finish();
        havePong = true;
        auto response = fromJson(LSMessageGetPayload(m));
        EXPECT_EQ(JValue(false), response["returnValue"]);
        EXPECT_EQ(JValue("Unknown method \"ping\" for category \"/\""), response["errorText"]) << toJson(response);
        return true;
    };

    EXPECT_TRUE(
        LSCallOneReply(
            sh_client.get(), "luna://com.palm.test/ping", "{}",
            wrap<&TestCategory::cbPong>(), this, &token,
            e.get()
        )
    ) << e.what();
    wait();
    ASSERT_TRUE(havePong);

    LSMethod methods[] = {
        { "ping", wrap<&TestCategory::cbPing>() },
        { nullptr, nullptr },
    };

    ASSERT_NO_THROW({ sh.registerCategory("/", methods, nullptr, nullptr); });
    ASSERT_NO_THROW({ sh.setCategoryData("/", this); });

    // call complete service for /ping
    havePing = havePong = false;
    ping = [&](LSMessage *m) {
        finish();
        havePing = true;
        LS::Error e;
        EXPECT_TRUE(LSMessageRespond(m, "{\"returnValue\":true, \"answer\":42}", e.get())) << e.what();
        return true;
    };
    pong = [&](LSMessage *m) {
        finish();
        havePong = true;
        auto response = fromJson(LSMessageGetPayload(m));
        EXPECT_EQ(JValue(true), response["returnValue"]);
        EXPECT_EQ(JValue(42), response["answer"]);
        return true;
    };

    EXPECT_TRUE(
        LSCallOneReply(
            sh_client.get(), "luna://com.palm.test/ping", "{}",
            wrap<&TestCategory::cbPong>(), this, &token,
            e.get()
        )
    ) << e.what();
    wait();
    EXPECT_TRUE(havePing);
    wait();
    EXPECT_TRUE(havePong);
}

TEST_F(TestCategory, Introspection)
{
    ASSERT_NO_THROW({ sh.registerCategory("/", nullptr, nullptr, nullptr); });

    JRef description {
        { "methods", {
            { "ping", {
                { "call", {
                    { "type", "object" },
                    { "description", "simple ping" },
                    { "additionalProperties", false },
                }},
            }},
        }},
    };

    ASSERT_NO_THROW({ sh.setCategoryDescription("/", description.get()); });

    // TODO: call for introspection
}

TEST_F(TestCategory, Validation)
{
    LS::Error e;
    LSMessageToken token;

    LSMethod methods[] = {
        { "ping", wrap<&TestCategory::cbPing>(), LUNA_METHOD_FLAG_VALIDATE_IN },
        { nullptr, nullptr },
    };

    ASSERT_NO_THROW({ sh.registerCategory("/", methods, nullptr, nullptr); });
    ASSERT_NO_THROW({ sh.setCategoryData("/", this); });

    JRef description {
        { "methods", {
            { "ping", {
                { "call", {
                    { "type", "object" },
                    { "description", "simple ping" },
                    { "additionalProperties", false },
                }},
            }},
        }},
    };

    ASSERT_NO_THROW({ sh.setCategoryDescription( "/", description.get()); });

    bool havePong = false, havePing = false;
    ping = [&](LSMessage *m) {
        finish();
        havePing = true;
        auto params = fromJson(LSMessageGetPayload(m));
        EXPECT_TRUE( params.isObject() );
        LS::Error e;
        EXPECT_TRUE(LSMessageRespond(m, "{\"returnValue\":true, \"answer\":42}", e.get())) << e.what();
        return true;
    };
    pong = [&](LSMessage *m) {
        finish();
        havePong = true;
        auto response = fromJson(LSMessageGetPayload(m));
        EXPECT_EQ(JRef(false), response["returnValue"]);
        return true;
    };
    EXPECT_TRUE(
        LSCallOneReply(
            sh_client.get(), "luna://com.palm.test/ping", "{\"wrong\":42}",
            wrap<&TestCategory::cbPong>(), this, &token,
            e.get()
        )
    ) << e.what();

    wait();
    EXPECT_FALSE(havePing);
    if (havePing) wait();
    EXPECT_TRUE(havePong);

    havePong = havePing = false;

    pong = [&](LSMessage *m) {
        finish();
        havePong = true;
        auto response = fromJson(LSMessageGetPayload(m));
        EXPECT_EQ(JRef(true), response["returnValue"]);
        EXPECT_EQ(JRef(42), response["answer"]);
        return true;
    };

    EXPECT_TRUE(
        LSCallOneReply(
            sh_client.get(), "luna://com.palm.test/ping", "{}",
            wrap<&TestCategory::cbPong>(), this, &token,
            e.get()
        )
    ) << e.what();

    wait();
    EXPECT_TRUE(havePing);
    wait();
    EXPECT_TRUE(havePong);
}
