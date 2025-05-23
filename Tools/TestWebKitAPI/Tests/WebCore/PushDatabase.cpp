/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "Utilities.h"
#include <WebCore/PushDatabase.h>
#include <WebCore/SQLiteDatabase.h>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <wtf/FileSystem.h>
#include <wtf/text/MakeString.h>

using namespace WebCore;

// Due to argument-dependent lookup, equality operators have to be in the WebCore namespace for gtest to find them.
namespace WebCore {

static bool operator==(const PushRecord& a, const PushRecord& b)
{
    return a.identifier == b.identifier
        && a.subscriptionSetIdentifier == b.subscriptionSetIdentifier
        && a.securityOrigin == b.securityOrigin
        && a.scope == b.scope
        && a.endpoint == b.endpoint
        && a.topic == b.topic
        && a.serverVAPIDPublicKey == b.serverVAPIDPublicKey
        && a.clientPublicKey == b.clientPublicKey
        && a.clientPrivateKey == b.clientPrivateKey
        && a.sharedAuthSecret == b.sharedAuthSecret
        && a.expirationTime == b.expirationTime;
}

static bool operator==(const PushSubscriptionSetRecord& a, const PushSubscriptionSetRecord& b)
{
    return a.identifier == b.identifier && a.securityOrigin == b.securityOrigin && a.enabled == b.enabled;
}

static bool operator==(const Vector<PushSubscriptionSetRecord>& a, const Vector<PushSubscriptionSetRecord>& b)
{
    if (a.size() != b.size())
        return false;

    // Not efficient, but ok for the sizes in this test.
    auto bCopy = b;
    for (auto& record : a) {
        if (!bCopy.removeFirst(record))
            return false;
    }

    return true;
}

static bool operator==(PushTopics a, PushTopics b)
{
    auto lessThan = [](const String& lhs, const String& rhs) {
        return codePointCompare(lhs, rhs) < 0;
    };

    std::ranges::sort(a.enabledTopics, lessThan);
    std::ranges::sort(a.ignoredTopics, lessThan);
    std::ranges::sort(b.enabledTopics, lessThan);
    std::ranges::sort(b.ignoredTopics, lessThan);

    return a.enabledTopics == b.enabledTopics
        && a.ignoredTopics == b.ignoredTopics;
}

}

namespace TestWebKitAPI {

template <typename T>
static HashSet<String> getTopicsFromRecords(const T& records) {
    HashSet<String> result;
    for (const auto& record : records)
        result.add(record.topic);
    return result;
};

static String makeTemporaryDatabasePath()
{
    return FileSystem::createTemporaryFile("PushDatabase"_s, ".db"_s);
}

static RefPtr<PushDatabase> createDatabaseSync(const String& path)
{
    RefPtr<PushDatabase> result;
    bool done = false;

    PushDatabase::create(path, [&done, &result](RefPtr<PushDatabase>&& database) {
        result = WTFMove(database);
        done = true;
    });
    Util::run(&done);

    return result;
}

static Vector<uint8_t> getPublicTokenSync(PushDatabase& database)
{
    bool done = false;
    Vector<uint8_t> getResult;

    database.getPublicToken([&done, &getResult](auto&& result) {
        getResult = WTFMove(result);
        done = true;
    });
    Util::run(&done);

    return getResult;
}

static PushDatabase::PublicTokenChanged updatePublicTokenSync(PushDatabase& database, std::span<const uint8_t> token)
{
    bool done = false;
    auto updateResult = PushDatabase::PublicTokenChanged::No;

    database.updatePublicToken(token, [&done, &updateResult](auto&& result) {
        updateResult = WTFMove(result);
        done = true;
    });
    Util::run(&done);

    return updateResult;
}

static std::optional<PushRecord> getRecordBySubscriptionSetAndScopeSync(PushDatabase& database, const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, const String& scope)
{
    bool done = false;
    std::optional<PushRecord> getResult;

    database.getRecordBySubscriptionSetAndScope(subscriptionSetIdentifier, scope, [&done, &getResult](std::optional<PushRecord>&& result) {
        getResult = WTFMove(result);
        done = true;
    });
    Util::run(&done);

    return getResult;
}

static HashSet<uint64_t> getRowIdentifiersSync(PushDatabase& database)
{
    bool done = false;
    HashSet<uint64_t> rowIdentifiers;

    database.getIdentifiers([&done, &rowIdentifiers](HashSet<PushSubscriptionIdentifier>&& identifiers) {
        for (auto identifier : identifiers)
            rowIdentifiers.add(identifier.toUInt64());
        done = true;
    });
    Util::run(&done);

    return rowIdentifiers;
}

static Vector<PushSubscriptionSetRecord> getPushSubscriptionSetsSync(PushDatabase& database)
{
    bool done = false;
    Vector<PushSubscriptionSetRecord> result;

    database.getPushSubscriptionSetRecords([&done, &result](auto&& records) {
        result = WTFMove(records);
        done = true;
    });
    Util::run(&done);

    return result;
}

static PushTopics getTopicsSync(PushDatabase& database)
{
    bool done = false;
    PushTopics topics;

    database.getTopics([&done, &topics](auto&& result) {
        topics = WTFMove(result);
        done = true;
    });
    Util::run(&done);

    return topics;
}

class PushDatabaseTest : public testing::Test {
public:
    RefPtr<PushDatabase> db;

    IGNORE_CLANG_WARNINGS_BEGIN("missing-designated-field-initializers")
    PushRecord record1 {
        .subscriptionSetIdentifier = { "com.apple.webapp"_s, emptyString(), std::nullopt },
        .securityOrigin = "https://www.apple.com"_s,
        .scope = "https://www.apple.com/iphone"_s,
        .endpoint = "https://pushEndpoint1"_s,
        .topic = "topic1"_s,
        .serverVAPIDPublicKey = { 1 },
        .clientPublicKey = { 2 },
        .clientPrivateKey = { 3 },
        .sharedAuthSecret = { 4 }
    };
    PushRecord record2 {
        .subscriptionSetIdentifier = { "com.apple.Safari"_s, emptyString(), std::nullopt },
        .securityOrigin = "https://www.webkit.org"_s,
        .scope = "https://www.webkit.org/blog"_s,
        .endpoint = "https://pushEndpoint2"_s,
        .topic = "topic2"_s,
        .serverVAPIDPublicKey = { 5 },
        .clientPublicKey = { 6 },
        .clientPrivateKey = { 7 },
        .sharedAuthSecret = { 8 }
    };
    PushRecord record3 {
        .subscriptionSetIdentifier = { "com.apple.Safari"_s, emptyString(), std::nullopt },
        .securityOrigin = "https://www.apple.com"_s,
        .scope = "https://www.apple.com/mac"_s,
        .endpoint = "https://pushEndpoint3"_s,
        .topic = "topic3"_s,
        .serverVAPIDPublicKey = { 9 },
        .clientPublicKey = { 10 },
        .clientPrivateKey = { 11 },
        .sharedAuthSecret = { 12 },
        .expirationTime = convertSecondsToEpochTimeStamp(1643350000),
    };
    PushRecord record4 {
        .subscriptionSetIdentifier = { "com.apple.Safari"_s, emptyString(), std::nullopt },
        .securityOrigin = "https://www.apple.com"_s,
        .scope = "https://www.apple.com/iphone"_s,
        .endpoint = "https://pushEndpoint4"_s,
        .topic = "topic4"_s,
        .serverVAPIDPublicKey = { 13 },
        .clientPublicKey = { 14 },
        .clientPrivateKey = { 15 },
        .sharedAuthSecret = { 16 }
    };
    PushRecord record5 {
        .subscriptionSetIdentifier = {
            .bundleIdentifier = "com.apple.webapp"_s,
            .pushPartition = emptyString(),
            .dataStoreIdentifier = WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        },
        .securityOrigin = "https://www.webkit.org"_s,
        .scope = "https://www.webkit.org/blog"_s,
        .endpoint = "https://pushEndpoint5"_s,
        .topic = "topic5"_s,
        .serverVAPIDPublicKey = { 17 },
        .clientPublicKey = { 18 },
        .clientPrivateKey = { 19 },
        .sharedAuthSecret = { 20 }
    };
    PushRecord record6 {
        .subscriptionSetIdentifier = {
            .bundleIdentifier = "com.apple.webapp"_s,
            .pushPartition = emptyString(),
            .dataStoreIdentifier = WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        },
        .securityOrigin = "https://www.apple.com"_s,
        .scope = "https://www.apple.com/iphone"_s,
        .endpoint = "https://pushEndpoint6"_s,
        .topic = "topic6"_s,
        .serverVAPIDPublicKey = { 21 },
        .clientPublicKey = { 22 },
        .clientPrivateKey = { 23 },
        .sharedAuthSecret = { 24 }
    };
    PushRecord record7 {
        .subscriptionSetIdentifier = {
            .bundleIdentifier = "com.apple.webapp"_s,
            .pushPartition = "partition"_s,
            .dataStoreIdentifier = WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        },
        .securityOrigin = "https://www.apple.com"_s,
        .scope = "https://www.apple.com/iphone"_s,
        .endpoint = "https://pushEndpoint7"_s,
        .topic = "topic7"_s,
        .serverVAPIDPublicKey = { 21 },
        .clientPublicKey = { 22 },
        .clientPrivateKey = { 23 },
        .sharedAuthSecret = { 24 }
    };
    IGNORE_CLANG_WARNINGS_END

    std::optional<PushRecord> insertResult1;
    std::optional<PushRecord> insertResult2;
    std::optional<PushRecord> insertResult3;
    std::optional<PushRecord> insertResult4;
    std::optional<PushRecord> insertResult5;
    std::optional<PushRecord> insertResult6;
    std::optional<PushRecord> insertResult7;
    Vector<PushSubscriptionSetRecord> expectedSubscriptionSets {
        // from record1
        { { "com.apple.webapp"_s, emptyString(), std::nullopt }, "https://www.apple.com"_s, true },

        // from record2
        { { "com.apple.Safari"_s, emptyString(), std::nullopt }, "https://www.webkit.org"_s, true },

        // from record3 and record4
        { { "com.apple.Safari"_s, emptyString(), std::nullopt }, "https://www.apple.com"_s, true },

        // from record5
        { { "com.apple.webapp"_s, emptyString(), WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        }, "https://www.webkit.org"_s, true },

        // from record6
        { { "com.apple.webapp"_s, emptyString(), WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        }, "https://www.apple.com"_s, true },

        // from record7
        { { "com.apple.webapp"_s, "partition"_s, WTF::UUID::parse("c1d79454-1f1a-4135-8764-e71d0de4b02e"_s)
        }, "https://www.apple.com"_s, true },
    };

    Vector<uint8_t> getPublicToken()
    {
        return getPublicTokenSync(*db);
    }

    PushDatabase::PublicTokenChanged updatePublicToken(std::span<const uint8_t> token)
    {
        return updatePublicTokenSync(*db, token);
    }

    std::optional<PushRecord> insertRecord(const PushRecord& record)
    {
        bool done = false;
        std::optional<PushRecord> insertResult;

        db->insertRecord(record, [&done, &insertResult](std::optional<PushRecord>&& result) {
            insertResult = WTFMove(result);
            done = true;
        });
        Util::run(&done);

        return insertResult;
    }

    bool removeRecordByRowIdentifier(uint64_t rowIdentifier)
    {
        bool done = false;
        bool removeResult = false;

        db->removeRecordByIdentifier(ObjectIdentifier<PushSubscriptionIdentifierType>(rowIdentifier), [&done, &removeResult](bool result) {
            removeResult = result;
            done = true;
        });
        Util::run(&done);

        return removeResult;
    }

    std::optional<PushRecord> getRecordByTopic(const String& topic)
    {
        bool done = false;
        std::optional<PushRecord> getResult;

        db->getRecordByTopic(topic, [&done, &getResult](std::optional<PushRecord>&& result) {
            getResult = WTFMove(result);
            done = true;
        });
        Util::run(&done);

        return getResult;
    }

    std::optional<PushRecord> getRecordBySubscriptionSetAndScope(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, const String& scope)
    {
        return getRecordBySubscriptionSetAndScopeSync(*db, subscriptionSetIdentifier, scope);
    }

    HashSet<uint64_t> getRowIdentifiers()
    {
        return getRowIdentifiersSync(*db);
    }

    Vector<PushSubscriptionSetRecord> getPushSubscriptionSets()
    {
        return getPushSubscriptionSetsSync(*db);
    }

    PushTopics getTopics()
    {
        return getTopicsSync(*db);
    }

    Vector<RemovedPushRecord> removeRecordsBySubscriptionSet(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier)
    {
        bool done = false;
        Vector<RemovedPushRecord> removedRecords;

        db->removeRecordsBySubscriptionSet(subscriptionSetIdentifier, [&done, &removedRecords](Vector<RemovedPushRecord>&& result) {
            removedRecords = WTFMove(result);
            done = true;
        });
        Util::run(&done);

        return removedRecords;
    }

    Vector<RemovedPushRecord> removeRecordsBySubscriptionSetAndSecurityOrigin(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, const String& securityOrigin)
    {
        bool done = false;
        Vector<RemovedPushRecord> removedRecords;

        db->removeRecordsBySubscriptionSetAndSecurityOrigin(subscriptionSetIdentifier, securityOrigin, [&done, &removedRecords](auto&& result) {
            removedRecords = WTFMove(result);
            done = true;
        });
        Util::run(&done);

        return removedRecords;
    }

    Vector<RemovedPushRecord> removeRecordsByBundleIdentifierAndDataStore(const String& bundleIdentifier, const std::optional<WTF::UUID>& dataStoreIdentifier)
    {
        bool done = false;
        Vector<RemovedPushRecord> removedRecords;

        db->removeRecordsByBundleIdentifierAndDataStore(bundleIdentifier, dataStoreIdentifier, [&done, &removedRecords](auto&& result) {
            removedRecords = WTFMove(result);
            done = true;
        });
        Util::run(&done);

        return removedRecords;
    }

    unsigned incrementSilentPushCount(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, const String& securityOrigin)
    {
        bool done = false;
        unsigned count = 0;

        db->incrementSilentPushCount(subscriptionSetIdentifier, securityOrigin, [&done, &count](int result) {
            count = result;
            done = true;
        });
        Util::run(&done);

        return count;
    }

    bool setPushesEnabled(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, bool enabled)
    {
        bool done = false;
        bool result = false;

        db->setPushesEnabled(subscriptionSetIdentifier, enabled, [&done, &result](bool recordsChanged) {
            result = recordsChanged;
            done = true;
        });
        Util::run(&done);

        return result;
    }

    bool setPushesEnabledForOrigin(const PushSubscriptionSetIdentifier& subscriptionSetIdentifier, const String& securityOrigin, bool enabled)
    {
        bool done = false;
        bool result = false;

        db->setPushesEnabledForOrigin(subscriptionSetIdentifier, securityOrigin, enabled, [&done, &result](bool recordsChanged) {
            result = recordsChanged;
            done = true;
        });
        Util::run(&done);

        return result;
    }

    void SetUp() final
    {
        db = createDatabaseSync(SQLiteDatabase::inMemoryPath());
        ASSERT_TRUE(db);
        ASSERT_TRUE(insertResult1 = insertRecord(record1));
        ASSERT_TRUE(insertResult2 = insertRecord(record2));
        ASSERT_TRUE(insertResult3 = insertRecord(record3));
        ASSERT_TRUE(insertResult4 = insertRecord(record4));
        ASSERT_TRUE(insertResult5 = insertRecord(record5));
        ASSERT_TRUE(insertResult6 = insertRecord(record6));
        ASSERT_TRUE(insertResult7 = insertRecord(record7));

    }
};

TEST_F(PushDatabaseTest, UpdatePublicToken)
{
    Vector<uint8_t> initialToken = { 'a', 'b', 'c' };
    Vector<uint8_t> modifiedToken = { 'd', 'e', 'f' };

    // Setting the initial public token shouldn't delete anything.
    auto updateResult1 = updatePublicToken(initialToken);
    EXPECT_EQ(updateResult1, PushDatabase::PublicTokenChanged::No);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 3, 4, 5, 6, 7 }));

    auto getResult1 = getPublicToken();
    EXPECT_EQ(getResult1, initialToken);

    // Setting the same token again should do nothing.
    auto updateResult2 = updatePublicToken(initialToken);
    EXPECT_EQ(updateResult2, PushDatabase::PublicTokenChanged::No);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 3, 4, 5, 6, 7 }));

    auto getResult2 = getPublicToken();
    EXPECT_EQ(getResult2, initialToken);

    // Changing the public token afterwards should delete everything.
    auto updateResult3 = updatePublicToken(modifiedToken);
    EXPECT_EQ(updateResult3, PushDatabase::PublicTokenChanged::Yes);
    EXPECT_TRUE(getRowIdentifiers().isEmpty());
    EXPECT_TRUE(getPushSubscriptionSets().isEmpty());

    auto getResult3 = getPublicToken();
    EXPECT_EQ(getResult3, modifiedToken);
}

TEST_F(PushDatabaseTest, InsertRecord)
{
    auto expectedRecord1 = record1;
    expectedRecord1.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(1);
    EXPECT_TRUE(expectedRecord1 == *insertResult1);

    auto expectedRecord2 = record2;
    expectedRecord2.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(2);
    EXPECT_TRUE(expectedRecord2 == *insertResult2);

    auto expectedRecord3 = record3;
    expectedRecord3.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(3);
    EXPECT_TRUE(expectedRecord3 == *insertResult3);

    auto expectedRecord4 = record4;
    expectedRecord4.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(4);
    EXPECT_TRUE(expectedRecord4 == *insertResult4);

    auto expectedRecord5 = record5;
    expectedRecord5.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(5);
    EXPECT_TRUE(expectedRecord5 == *insertResult5);

    auto expectedRecord6 = record6;
    expectedRecord6.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(6);
    EXPECT_TRUE(expectedRecord6 == *insertResult6);
    
    auto expectedRecord7 = record7;
    expectedRecord7.identifier = ObjectIdentifier<PushSubscriptionIdentifierType>(7);
    EXPECT_TRUE(expectedRecord7 == *insertResult7);

    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    // Inserting a record with the same (subscriptionSetIdentifier, scope) as record 1 should fail.
    PushRecord record8 = record1;
    record8.topic = "topic8"_s;
    EXPECT_FALSE(insertRecord(WTFMove(record8)));
    
    // Inserting a record that has a different dataStoreIdentifier should succeed.
    PushRecord record9 = record1;
    record9.subscriptionSetIdentifier.dataStoreIdentifier = WTF::UUID::createVersion4Weak();
    record9.topic = "topic9"_s;
    EXPECT_TRUE(insertRecord(WTFMove(record9)));

    // Inserting a record that has a different pushPartition should succeed.
    PushRecord record10 = record1;
    record10.subscriptionSetIdentifier.pushPartition = "foobar"_s;
    record10.topic = "topic10"_s;
    EXPECT_TRUE(insertRecord(WTFMove(record10)));

    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 3, 4, 5, 6, 7, 8, 9 }));

    expectedSubscriptionSets.append({ record9.subscriptionSetIdentifier, record9.securityOrigin, true });
    expectedSubscriptionSets.append({ record10.subscriptionSetIdentifier, record10.securityOrigin, true });
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, RemoveRecord)
{
    EXPECT_TRUE(removeRecordByRowIdentifier(1));
    EXPECT_FALSE(removeRecordByRowIdentifier(1));
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 2, 3, 4, 5, 6, 7 }));

    auto removeResult = expectedSubscriptionSets.removeFirst(PushSubscriptionSetRecord { record1.subscriptionSetIdentifier, record1.securityOrigin, true });
    EXPECT_TRUE(removeResult);
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, RemoveRecordsBySubscriptionSet)
{
    // record2, record3, and record4 have the same subscriptionSetIdentifier.
    auto removedRecords = removeRecordsBySubscriptionSet(record2.subscriptionSetIdentifier);
    auto expected = HashSet<String> { record2.topic, record3.topic, record4.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 3u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 5, 6, 7 }));

    // record5 and record6 have the same subscriptionSetIdentifier.
    removedRecords = removeRecordsBySubscriptionSet(record5.subscriptionSetIdentifier);
    expected = HashSet<String> { record5.topic, record6.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 2u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 7 }));

    // record7 has its own subscriptionSetIdentifier.
    removedRecords = removeRecordsBySubscriptionSet(record7.subscriptionSetIdentifier);
    expected = HashSet<String> { record7.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 1u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1 }));

    // Inserting a new record should produce a new identifier.
    PushRecord record8 = record3;
    auto insertResult = insertRecord(WTFMove(record8));
    EXPECT_TRUE(insertResult);
    EXPECT_EQ(insertResult->identifier, ObjectIdentifier<PushSubscriptionIdentifierType>(8));
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 8 }));

    Vector<PushSubscriptionSetRecord> expectedSubscriptionSets {
        { record1.subscriptionSetIdentifier, record1.securityOrigin, true },
        { record8.subscriptionSetIdentifier, record8.securityOrigin, true }
    };
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, RemoveRecordsBySubscriptionSetAndSecurityOrigin)
{
    // record3 and record4 have the same subscriptionSetIdentifier.
    auto removedRecords = removeRecordsBySubscriptionSetAndSecurityOrigin(record3.subscriptionSetIdentifier, record3.securityOrigin);
    auto expected = HashSet<String> { record3.topic, record4.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 2u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 5, 6, 7 }));

    // record5 and record6 have the same subscriptionSetIdentifier.
    removedRecords = removeRecordsBySubscriptionSetAndSecurityOrigin(record6.subscriptionSetIdentifier, record6.securityOrigin);
    expected = HashSet<String> { record6.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 1u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 5, 7 }));

    // record7 has a distinct subscriptionSetIdentifier.
    removedRecords = removeRecordsBySubscriptionSetAndSecurityOrigin(record7.subscriptionSetIdentifier, record7.securityOrigin);
    expected = HashSet<String> { record7.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 1u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 5 }));

    // Inserting a new record should produce a new identifier.
    PushRecord record8 = record3;
    auto insertResult = insertRecord(WTFMove(record8));
    EXPECT_TRUE(insertResult);
    EXPECT_EQ(insertResult->identifier, ObjectIdentifier<PushSubscriptionIdentifierType>(8));
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 5, 8 }));

    Vector<PushSubscriptionSetRecord> expectedSubscriptionSets {
        { record1.subscriptionSetIdentifier, record1.securityOrigin, true },
        { record2.subscriptionSetIdentifier, record2.securityOrigin, true },
        { record5.subscriptionSetIdentifier, record5.securityOrigin, true },
        { record8.subscriptionSetIdentifier, record8.securityOrigin, true }
    };
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, RemoveRecordsByBundleIdentifierAndDataStore)
{
    // record5, record6, and record7 have the same bundleIdentifier and dataStoreIdentifier, but different pushPartitions.
    auto removedRecords = removeRecordsByBundleIdentifierAndDataStore(record5.subscriptionSetIdentifier.bundleIdentifier, record5.subscriptionSetIdentifier.dataStoreIdentifier);
    auto expected = HashSet<String> { record5.topic, record6.topic, record7.topic };
    EXPECT_EQ(getTopicsFromRecords(removedRecords), expected);
    EXPECT_EQ(removedRecords.size(), 3u);
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 3, 4 }));

    // Inserting a new record should produce a new identifier.
    PushRecord record8 = record5;
    auto insertResult = insertRecord(WTFMove(record8));
    EXPECT_TRUE(insertResult);
    EXPECT_EQ(insertResult->identifier, ObjectIdentifier<PushSubscriptionIdentifierType>(8));
    EXPECT_EQ(getRowIdentifiers(), (HashSet<uint64_t> { 1, 2, 3, 4, 8 }));

    Vector<PushSubscriptionSetRecord> expectedSubscriptionSets {
        { record1.subscriptionSetIdentifier, record1.securityOrigin, true },
        { record2.subscriptionSetIdentifier, record2.securityOrigin, true },
        { record3.subscriptionSetIdentifier, record3.securityOrigin, true },
        { record5.subscriptionSetIdentifier, record5.securityOrigin, true },
    };
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, GetRecordByTopic)
{
    auto result1 = getRecordByTopic(record1.topic);
    ASSERT_TRUE(result1);
    EXPECT_EQ(*result1, *insertResult1);

    auto result2 = getRecordByTopic("foo"_s);
    EXPECT_FALSE(result2);
}

TEST_F(PushDatabaseTest, GetRecordBySubscriptionSetAndScope)
{
    auto result1 = getRecordBySubscriptionSetAndScope(record1.subscriptionSetIdentifier, record1.scope);
    ASSERT_TRUE(result1);
    EXPECT_EQ(*result1, *insertResult1);

    auto result2 = getRecordBySubscriptionSetAndScope(record1.subscriptionSetIdentifier, "bar"_s);
    EXPECT_FALSE(result2);

    auto result3 = getRecordBySubscriptionSetAndScope(PushSubscriptionSetIdentifier { "foo"_s, emptyString(), std::nullopt }, record1.scope);
    EXPECT_FALSE(result3);
    
    auto result4 = getRecordBySubscriptionSetAndScope(record4.subscriptionSetIdentifier, record4.scope);
    ASSERT_TRUE(result4);
    EXPECT_EQ(*result4, *insertResult4);
    
    auto result5 = getRecordBySubscriptionSetAndScope(record5.subscriptionSetIdentifier, record5.scope);
    ASSERT_TRUE(result5);
    EXPECT_EQ(*result5, *insertResult5);
    
    auto result7 = getRecordBySubscriptionSetAndScope(record7.subscriptionSetIdentifier, record7.scope);
    ASSERT_TRUE(result7);
    EXPECT_EQ(*result7, *insertResult7);
}

TEST_F(PushDatabaseTest, GetTopics)
{
    PushTopics expected { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { } };
    EXPECT_EQ(getTopics(), expected);
}

TEST_F(PushDatabaseTest, IncrementSilentPushCount)
{
    auto count = incrementSilentPushCount(record1.subscriptionSetIdentifier, record1.securityOrigin);
    EXPECT_EQ(count, 1u);

    // record1 and record3 have different subscriptionSetIdentifiers.
    count = incrementSilentPushCount(record3.subscriptionSetIdentifier, record3.securityOrigin);
    EXPECT_EQ(count, 1u);

    // record3 and record4 have the same subscriptionSetIdentifier.
    count = incrementSilentPushCount(record4.subscriptionSetIdentifier, record4.securityOrigin);
    EXPECT_EQ(count, 2u);
    
    // record5 has a distinct subscriptionSetIdentifier.
    count = incrementSilentPushCount(record5.subscriptionSetIdentifier, record5.securityOrigin);
    EXPECT_EQ(count, 1u);

    // record6 has a distinct subscriptionSetIdentifier.
    count = incrementSilentPushCount(record6.subscriptionSetIdentifier, record6.securityOrigin);
    EXPECT_EQ(count, 1u);
    
    // record7 has a distinct subscriptionSetIdentifier.
    count = incrementSilentPushCount(record7.subscriptionSetIdentifier, record7.securityOrigin);
    EXPECT_EQ(count, 1u);

    count = incrementSilentPushCount(PushSubscriptionSetIdentifier { "foobar"_s, emptyString(), std::nullopt }, "nonexistent"_s);
    EXPECT_EQ(count, 0u);
}

TEST_F(PushDatabaseTest, SetPushesEnabled)
{
    // topic2, topic3, topic4 have the same subscription set identifier.
    bool result = setPushesEnabled(record3.subscriptionSetIdentifier, false);
    EXPECT_TRUE(result);

    auto topics = getTopics();
    auto expectedTopics = PushTopics { { "topic1"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { "topic2"_s, "topic3"_s, "topic4"_s } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(1).enabled = false;
    expectedSubscriptionSets.at(2).enabled = false;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabled(record3.subscriptionSetIdentifier, true);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(1).enabled = true;
    expectedSubscriptionSets.at(2).enabled = true;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    // topic7 has a distinct subscriptionSetIdentifier.
    result = setPushesEnabled(record7.subscriptionSetIdentifier, false);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s }, { "topic7"_s } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(5).enabled = false;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabledForOrigin(record7.subscriptionSetIdentifier, record7.securityOrigin, true);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(5).enabled = true;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabledForOrigin(PushSubscriptionSetIdentifier { "foobar"_s, emptyString(), std::nullopt }, "https://www.apple.com"_s, false);
    EXPECT_FALSE(result);

    EXPECT_EQ(topics, expectedTopics);
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST_F(PushDatabaseTest, SetPushesEnabledForOrigin)
{
    // topic3 and topic4 both have the same subscriptionSetIdentifier and origin.
    bool result = setPushesEnabledForOrigin(record3.subscriptionSetIdentifier, record3.securityOrigin, false);
    EXPECT_TRUE(result);

    auto topics = getTopics();
    auto expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { "topic3"_s, "topic4"_s } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(2).enabled = false;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabledForOrigin(record3.subscriptionSetIdentifier, record3.securityOrigin, true);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(2).enabled = true;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    // topic7 has a distinct subscriptionSetIdentifier and origin.
    result = setPushesEnabledForOrigin(record7.subscriptionSetIdentifier, record7.securityOrigin, false);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s }, { "topic7"_s } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(5).enabled = false;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabledForOrigin(record7.subscriptionSetIdentifier, record7.securityOrigin, true);
    EXPECT_TRUE(result);

    topics = getTopics();
    expectedTopics = PushTopics { { "topic1"_s, "topic2"_s, "topic3"_s, "topic4"_s, "topic5"_s, "topic6"_s, "topic7"_s }, { } };
    EXPECT_EQ(topics, expectedTopics);

    expectedSubscriptionSets.at(5).enabled = true;
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);

    result = setPushesEnabledForOrigin(PushSubscriptionSetIdentifier { "foobar"_s, emptyString(), std::nullopt }, "https://www.apple.com"_s, false);
    EXPECT_FALSE(result);

    EXPECT_EQ(topics, expectedTopics);
    EXPECT_EQ(getPushSubscriptionSets(), expectedSubscriptionSets);
}

TEST(PushDatabase, ManyInFlightOps)
{
    auto path = makeTemporaryDatabasePath();
    constexpr unsigned recordCount = 256;

    {
        auto createResult = createDatabaseSync(path);
        ASSERT_TRUE(createResult);

        auto& database = *createResult;
        IGNORE_CLANG_WARNINGS_BEGIN("missing-designated-field-initializers")
        PushRecord record {
            .subscriptionSetIdentifier = PushSubscriptionSetIdentifier { "com.apple.Safari"_s, emptyString(), std::nullopt },
            .securityOrigin = "https://www.webkit.org"_s,
            .endpoint = "https://pushEndpoint1"_s,
            .serverVAPIDPublicKey = { 0, 1 },
            .clientPublicKey = { 1, 2 },
            .clientPrivateKey = { 2, 3 },
            .sharedAuthSecret = { 4, 5 },
            .expirationTime = convertSecondsToEpochTimeStamp(1643350000),
        };
        IGNORE_CLANG_WARNINGS_END

        for (unsigned i = 0; i < recordCount; i++) {
            record.scope = makeString("http://www.webkit.org/test/"_s, i);
            record.topic = makeString("topic_"_s, i);

            database.insertRecord(record, [](auto&& result) {
                ASSERT_TRUE(result);
            });
        }
    }

    {
        auto createResult = createDatabaseSync(path);
        ASSERT_TRUE(createResult);

        auto topics = getTopicsSync(*createResult);
        EXPECT_EQ(topics.enabledTopics.size(), recordCount);
    }
}

TEST(PushDatabase, StartsFromScratchOnDowngrade)
{
    auto path = makeTemporaryDatabasePath();

    {
        SQLiteDatabase db;
        db.open(path);
        ASSERT_TRUE(db.executeCommand("PRAGMA user_version = 100000"_s));
    }

    {
        auto createResult = createDatabaseSync(path);
        ASSERT_TRUE(createResult);
    }

    {
        SQLiteDatabase db;
        db.open(path);
        {
            int version = 0;
            auto sql = db.prepareStatement("PRAGMA user_version"_s);
            if (sql && sql->step() == SQLITE_ROW)
                version = sql->columnInt(0);
            EXPECT_GT(version, 0);
            EXPECT_LT(version, 100000);
        }
    }
}

static bool createDatabaseFromStatements(String path, ASCIILiteral* statements, size_t count)
{
    SQLiteDatabase db;
    db.open(path);

    for (size_t i = 0; i < count; i++) {
        if (!db.executeCommand(statements[i]))
            return false;
    }

    return true;
}

// Acquired by running .dump from the sqlite3 shell on a V2 database.
static ASCIILiteral pushDatabaseV2Statements[] = {
    "CREATE TABLE SubscriptionSets(  rowID INTEGER PRIMARY KEY AUTOINCREMENT,  creationTime INT NOT NULL,  bundleID TEXT NOT NULL,  securityOrigin TEXT NOT NULL,  silentPushCount INT NOT NULL,  UNIQUE(bundleID, securityOrigin))"_s,
    "INSERT INTO SubscriptionSets VALUES(1,1649541001,'com.apple.webapp','https://www.apple.com',0)"_s,
    "INSERT INTO SubscriptionSets VALUES(2,1649541001,'com.apple.Safari','https://www.webkit.org',0)"_s,
    "INSERT INTO SubscriptionSets VALUES(3,1649541001,'com.apple.Safari','https://www.apple.com',0)"_s,
    "CREATE TABLE Subscriptions(  rowID INTEGER PRIMARY KEY AUTOINCREMENT,  creationTime INT NOT NULL,  subscriptionSetID INT NOT NULL,  scope TEXT NOT NULL,  endpoint TEXT NOT NULL,  topic TEXT NOT NULL UNIQUE,  serverVAPIDPublicKey BLOB NOT NULL,  clientPublicKey BLOB NOT NULL,  clientPrivateKey BLOB NOT NULL,  sharedAuthSecret BLOB NOT NULL,  expirationTime INT,  UNIQUE(scope, subscriptionSetID))"_s,
    "INSERT INTO Subscriptions VALUES(1,1649541001,1,'https://www.apple.com/iphone','https://pushEndpoint1','topic1',X'0506',X'0607',X'0708',X'0809',NULL)"_s,
    "INSERT INTO Subscriptions VALUES(2,1649541001,2,'https://www.webkit.org/blog','https://pushEndpoint2','topic2',X'0e0f',X'1011',X'1213',X'1415',NULL)"_s,
    "INSERT INTO Subscriptions VALUES(3,1649541001,3,'https://www.apple.com/mac','https://pushEndpoint3','topic3',X'0001',X'0102',X'0203',X'0405',1643350000)"_s,
    "INSERT INTO Subscriptions VALUES(4,1649541001,3,'https://www.apple.com/iphone','https://pushEndpoint4','topic4',X'090a',X'0a0b',X'0b0c',X'0c0d',NULL)"_s,
    "DELETE FROM sqlite_sequence"_s,
    "INSERT INTO sqlite_sequence VALUES('SubscriptionSets',3)"_s,
    "INSERT INTO sqlite_sequence VALUES('Subscriptions',4)"_s,
    "CREATE INDEX Subscriptions_SubscriptionSetID_Index ON Subscriptions(subscriptionSetID)"_s,
    "PRAGMA user_version = 2"_s,
};

TEST(PushDatabase, CanMigrateV2DatabaseToCurrentSchema)
{
    auto path = makeTemporaryDatabasePath();
    ASSERT_TRUE(createDatabaseFromStatements(path, pushDatabaseV2Statements, std::size(pushDatabaseV2Statements)));

    // Make sure records are there after migrating.
    {
        auto databaseResult = createDatabaseSync(path);
        ASSERT_TRUE(databaseResult);
        auto& database = *databaseResult;

        auto recordResult = getRecordBySubscriptionSetAndScopeSync(database, PushSubscriptionSetIdentifier { "com.apple.Safari"_s, emptyString(), std::nullopt }, "https://www.webkit.org/blog"_s);
        ASSERT_TRUE(recordResult);
        ASSERT_EQ(recordResult->topic, "topic2"_s);

        auto rowIdentifiers = getRowIdentifiersSync(database);
        ASSERT_EQ(rowIdentifiers, (HashSet<uint64_t> { 1, 2, 3, 4 }));

        // Setting the initial token should return PublicTokenChanged::No.
        auto updateResult = updatePublicTokenSync(database, Vector<uint8_t> { 'a', 'b' });
        ASSERT_EQ(updateResult, PushDatabase::PublicTokenChanged::No);
    }

    // Make sure records are there after re-opening without migration.
    {
        auto databaseResult = createDatabaseSync(path);
        ASSERT_TRUE(databaseResult);
        auto& database = *databaseResult;

        auto getResult = getPublicTokenSync(database);
        ASSERT_EQ(getResult, (Vector<uint8_t> { 'a', 'b' }));

        auto rowIdentifiers = getRowIdentifiersSync(database);
        ASSERT_EQ(rowIdentifiers, (HashSet<uint64_t> { 1, 2, 3, 4 }));
    }
}

} // namespace TestWebKitAPI
