/*
 * Copyright (C) 2014-2024 Apple Inc. All rights reserved.
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
#include "NetworkCacheStorage.h"

#include "AuxiliaryProcess.h"
#include "Logging.h"
#include "NetworkCacheCoders.h"
#include "NetworkCacheFileSystem.h"
#include "NetworkCacheIOChannel.h"
#include <mutex>
#include <wtf/Condition.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/FileSystem.h>
#include <wtf/Lock.h>
#include <wtf/PageBlock.h>
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/persistence/PersistentCoders.h>
#include <wtf/persistence/PersistentDecoder.h>
#include <wtf/persistence/PersistentEncoder.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringToIntegerConversion.h>

namespace WebKit {
namespace NetworkCache {

static constexpr auto saltFileName = "salt"_s;
static constexpr auto versionDirectoryPrefix = "Version "_s;
static constexpr auto recordsDirectoryName = "Records"_s;
static constexpr auto blobsDirectoryName = "Blobs"_s;
static constexpr auto blobSuffix = "-blob"_s;

WTF_MAKE_TZONE_ALLOCATED_IMPL(Storage::Record);
WTF_MAKE_TZONE_ALLOCATED_IMPL(Storage::Timings);

static inline size_t maximumInlineBodySize()
{
    return WTF::pageSize();
}

static double computeRecordWorth(FileTimes);

class Storage::ReadOperation {
    WTF_MAKE_TZONE_ALLOCATED(Storage::ReadOperation);
public:
    ReadOperation(const Key& key, unsigned priority, RetrieveCompletionHandler&& completionHandler)
        : m_identifier(Storage::ReadOperationIdentifier::generate())
        , m_key(key)
        , m_priority(priority)
        , m_completionHandler(WTFMove(completionHandler))
    {
        ASSERT(isMainRunLoop());
        ASSERT(m_completionHandler);
    }

    Storage::ReadOperationIdentifier identifier() const { return m_identifier; }
    const Key& key() const { return m_key; }
    unsigned priority() const { return m_priority; }
    bool isCanceled() const { return m_isCanceled; }
    bool canFinish() const { return !m_waitsForRecord && !m_waitsForBlob; }

    void updateForStart(size_t readOperationDispatchCount);
    void updateForDispatch(bool synchronizationInProgress, bool shrinkInProgress, size_t readOperationDispatchCount);
    void setWaitsForBlob() { m_waitsForBlob = true; }
    void finishReadRecord(Record&&, MonotonicTime recordIOStartTime, MonotonicTime recordIOEndTime);
    void finishReadBlob(BlobStorage::Blob&&, MonotonicTime blobIOStartTime, MonotonicTime blobIOEndTime);
    void cancel();
    bool finish();

private:
    Storage::ReadOperationIdentifier m_identifier;
    const Key m_key;
    unsigned m_priority;
    RetrieveCompletionHandler m_completionHandler;

    bool m_waitsForRecord { true };
    bool m_waitsForBlob { false };
    bool m_isCanceled { false };
    Timings m_timings;
    Record m_record;
    std::optional<SHA1::Digest> m_blobBodyHash;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(Storage::ReadOperation);

bool Storage::isHigherPriority(const std::unique_ptr<ReadOperation>& a, const std::unique_ptr<ReadOperation>& b)
{
    if (a->priority() == b->priority())
        return a->identifier() < b->identifier();

    return a->priority() > b->priority();
}

void Storage::ReadOperation::updateForStart(size_t readOperationDispatchCount)
{
    ASSERT(RunLoop::isMain());

    m_timings.startTime = MonotonicTime::now();
    m_timings.dispatchCountAtStart = readOperationDispatchCount;
}

void Storage::ReadOperation::updateForDispatch(bool synchronizationInProgress, bool shrinkInProgress, size_t readOperationDispatchCount)
{
    ASSERT(RunLoop::isMain());

    m_timings.dispatchTime = MonotonicTime::now();
    m_timings.synchronizationInProgressAtDispatch = synchronizationInProgress;
    m_timings.shrinkInProgressAtDispatch = shrinkInProgress;
    m_timings.dispatchCountAtDispatch = readOperationDispatchCount;
}

void Storage::ReadOperation::cancel()
{
    ASSERT(RunLoop::isMain());

    if (m_isCanceled)
        return;

    m_isCanceled = true;
    m_timings.completionTime = MonotonicTime::now();
    m_timings.wasCanceled = true;

    if (m_completionHandler)
        m_completionHandler({ }, m_timings);
}

bool Storage::ReadOperation::finish()
{
    ASSERT(RunLoop::isMain());
    ASSERT(canFinish());

    if (!m_completionHandler)
        return false;

    m_timings.completionTime = MonotonicTime::now();

    if (m_record.key != m_key)
        m_record = { };

    // Blob and record are read separately, so we need to check if blob hash matches record hash.
    if (m_blobBodyHash && m_blobBodyHash != m_record.bodyHash)
        m_record = { };

    // Failed to read body from both blob storage and record storage.
    if (m_record.body.isNull())
        m_record = { };

    return m_completionHandler(WTFMove(m_record), m_timings);
}

void Storage::ReadOperation::finishReadRecord(Record&& record, MonotonicTime recordIOStartTime, MonotonicTime recordIOEndTime)
{
    ASSERT(RunLoop::isMain());

    m_waitsForRecord = false;
    // Body is already read from blob storage, and it is not null.
    if (m_blobBodyHash) {
        // Body should not be stored in both blob storage and record storage.
        ASSERT(record.body.isNull());
        record.body = WTFMove(m_record.body);
    }
    m_record = WTFMove(record);
    m_timings.recordIOStartTime = recordIOStartTime;
    m_timings.recordIOEndTime = recordIOEndTime;
}

void Storage::ReadOperation::finishReadBlob(BlobStorage::Blob&& blob, MonotonicTime blobIOStartTime, MonotonicTime blobIOEndTime)
{
    ASSERT(RunLoop::isMain());

    m_waitsForBlob = false;
    if (blob.data.isNull())
        return;

    m_record.body = WTFMove(blob.data);
    m_blobBodyHash = WTFMove(blob.hash);
    m_timings.blobIOStartTime = blobIOStartTime;
    m_timings.blobIOEndTime = blobIOEndTime;
}

class Storage::WriteOperation {
    WTF_MAKE_TZONE_ALLOCATED(Storage::WriteOperation);
public:
    WriteOperation(const Record& record, MappedBodyHandler&& mappedBodyHandler)
        : m_identifier(Storage::WriteOperationIdentifier::generate())
        , m_record(record)
        , m_mappedBodyHandler(WTFMove(mappedBodyHandler))
    {
        ASSERT(isMainRunLoop());
    }

    ~WriteOperation()
    {
        ASSERT(isMainRunLoop());
    }

    Storage::WriteOperationIdentifier identifier() const { return m_identifier; }
    const Record& record() const WTF_REQUIRES_CAPABILITY(mainThread) { return m_record; }
    void invokeMappedBodyHandler(const Data&);

private:
    Storage::WriteOperationIdentifier m_identifier;
    const Record m_record;
    const MappedBodyHandler m_mappedBodyHandler;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(Storage::WriteOperation);

void Storage::WriteOperation::invokeMappedBodyHandler(const Data& data)
{
    if (m_mappedBodyHandler)
        m_mappedBodyHandler(data);
}

class TraverseOperation final : public ThreadSafeRefCounted<TraverseOperation, WTF::DestructionThread::MainRunLoop> {
public:
    static Ref<TraverseOperation> create(Storage::TraverseHandler&& handler)
    {
        ASSERT(isMainRunLoop());
        return adoptRef(*new TraverseOperation(WTFMove(handler)));
    }

    void invokeHandler(const Storage::Record* record, const Storage::RecordInfo& info)
    {
        ASSERT(isMainRunLoop());
        m_handler(record, info);
    }

    static constexpr unsigned maxParallelActivityCount = 5;
    void waitAndIncrementActivityCount()
    {
        Locker locker { m_lock };
        m_activeCondition.wait(m_lock, [this] {
            assertIsHeld(m_lock);
            return m_activityCount < maxParallelActivityCount;
        });
        ++m_activityCount;
    }

    void decrementActivityCount()
    {
        Locker locker { m_lock };
        --m_activityCount;
        m_activeCondition.notifyOne();
    }

    void waitUntilActivitiesFinished()
    {
        Locker locker { m_lock };
        m_activeCondition.wait(m_lock, [this] {
            assertIsHeld(m_lock);
            return !m_activityCount;
        });
    }

private:
    explicit TraverseOperation(Storage::TraverseHandler&& handler)
        : m_handler(WTFMove(handler))
    { }

    Storage::TraverseHandler m_handler;
    Lock m_lock;
    Condition m_activeCondition;
    unsigned m_activityCount WTF_GUARDED_BY_LOCK(m_lock) { 0 };
};

static String makeCachePath(const String& baseCachePath)
{
#if PLATFORM(MAC)
    // Put development cache to a different directory to avoid affecting the system cache.
    if (!AuxiliaryProcess::isSystemWebKit())
        return FileSystem::pathByAppendingComponent(baseCachePath, "Development"_s);
#endif
    return baseCachePath;
}

static String makeVersionedDirectoryPath(const String& baseDirectoryPath)
{
    return FileSystem::pathByAppendingComponent(baseDirectoryPath, makeString(versionDirectoryPrefix, Storage::version));
}

static String makeRecordsDirectoryPath(const String& baseDirectoryPath)
{
    return FileSystem::pathByAppendingComponent(makeVersionedDirectoryPath(baseDirectoryPath), recordsDirectoryName);
}

static String makeBlobDirectoryPath(const String& baseDirectoryPath)
{
    return FileSystem::pathByAppendingComponent(makeVersionedDirectoryPath(baseDirectoryPath), blobsDirectoryName);
}

static String makeSaltFilePath(const String& baseDirectoryPath)
{
    return FileSystem::pathByAppendingComponent(makeVersionedDirectoryPath(baseDirectoryPath), saltFileName);
}

RefPtr<Storage> Storage::open(const String& baseCachePath, Mode mode, size_t capacity)
{
    ASSERT(RunLoop::isMain());
    ASSERT(!baseCachePath.isNull());

    auto cachePath = makeCachePath(baseCachePath);
    bool hasMarkedExcludedFromBackup = false;
    if (cachePath != baseCachePath) {
        if (!FileSystem::makeAllDirectories(cachePath))
            return nullptr;

        FileSystem::setExcludedFromBackup(cachePath, true);
        hasMarkedExcludedFromBackup = true;
    }

    auto versionedDirectoryPath = makeVersionedDirectoryPath(cachePath);
    if (!FileSystem::makeAllDirectories(versionedDirectoryPath))
        return nullptr;

    if (!hasMarkedExcludedFromBackup)
        FileSystem::setExcludedFromBackup(versionedDirectoryPath, true);

    auto salt = FileSystem::readOrMakeSalt(makeSaltFilePath(cachePath));
    if (!salt)
        return nullptr;

    return adoptRef(new Storage(cachePath, mode, *salt, capacity));
}

using RecordFileTraverseFunction = Function<void (const String& fileName, const String& hashString, const String& type, bool isBlob, const String& recordDirectoryPath)>;
static void traverseRecordsFiles(const String& recordsPath, const String& expectedType, NOESCAPE const RecordFileTraverseFunction& function)
{
    traverseDirectory(recordsPath, [&](const String& partitionName, DirectoryEntryType entryType) {
        if (entryType != DirectoryEntryType::Directory)
            return;
        String partitionPath = FileSystem::pathByAppendingComponent(recordsPath, partitionName);
        traverseDirectory(partitionPath, [&](const String& actualType, DirectoryEntryType entryType) {
            if (entryType != DirectoryEntryType::Directory)
                return;
            if (!expectedType.isEmpty() && expectedType != actualType)
                return;
            String recordDirectoryPath = FileSystem::pathByAppendingComponent(partitionPath, actualType);
            traverseDirectory(recordDirectoryPath, [&function, &recordDirectoryPath, &actualType](const String& fileName, DirectoryEntryType entryType) {
                if (entryType != DirectoryEntryType::File || fileName.length() < Key::hashStringLength())
                    return;

                String hashString = fileName.left(Key::hashStringLength());
                auto isBlob = fileName.length() > Key::hashStringLength() && fileName.endsWith(blobSuffix);
                function(fileName, hashString, actualType, isBlob, recordDirectoryPath);
            });
        });
    });
}

static void deleteEmptyRecordsDirectories(const String& recordsPath)
{
    traverseDirectory(recordsPath, [&recordsPath](const String& partitionName, DirectoryEntryType type) {
        if (type != DirectoryEntryType::Directory)
            return;

        // Delete [type] sub-folders.
        String partitionPath = FileSystem::pathByAppendingComponent(recordsPath, partitionName);
        traverseDirectory(partitionPath, [&partitionPath](const String& subdirName, DirectoryEntryType entryType) {
            if (entryType != DirectoryEntryType::Directory)
                return;

            // Let system figure out if it is really empty.
            FileSystem::deleteEmptyDirectory(FileSystem::pathByAppendingComponent(partitionPath, subdirName));
        });

        // Delete [Partition] folders.
        // Let system figure out if it is really empty.
        FileSystem::deleteEmptyDirectory(FileSystem::pathByAppendingComponent(recordsPath, partitionName));
    });
}

Storage::Storage(const String& baseDirectoryPath, Mode mode, Salt salt, size_t capacity)
    : m_basePath(baseDirectoryPath)
    , m_recordsPath(makeRecordsDirectoryPath(baseDirectoryPath))
    , m_mode(mode)
    , m_salt(salt)
    , m_capacity(capacity)
    , m_readOperationTimeoutTimer(*this, &Storage::cancelAllReadOperations)
    , m_writeOperationDispatchTimer(*this, &Storage::dispatchPendingWriteOperations)
    , m_ioQueue(ConcurrentWorkQueue::create("com.apple.WebKit.Cache.Storage"_s, WorkQueue::QOS::UserInteractive))
    , m_backgroundIOQueue(ConcurrentWorkQueue::create("com.apple.WebKit.Cache.Storage.background"_s, WorkQueue::QOS::Utility))
    , m_serialBackgroundIOQueue(WorkQueue::create("com.apple.WebKit.Cache.Storage.serialBackground"_s, WorkQueue::QOS::Utility))
    , m_blobStorage(makeBlobDirectoryPath(baseDirectoryPath), m_salt)
{
    ASSERT(RunLoop::isMain());

    deleteOldVersions();
    synchronize();
}

Storage::~Storage()
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_activeReadOperations.isEmpty());
    ASSERT(m_activeWriteOperations.isEmpty());
    ASSERT(!m_synchronizationInProgress);
    ASSERT(!m_shrinkInProgress);
}

String Storage::basePathIsolatedCopy() const
{
    return m_basePath.isolatedCopy();
}

String Storage::versionPath() const
{
    return makeVersionedDirectoryPath(basePathIsolatedCopy());
}

String Storage::recordsPathIsolatedCopy() const
{
    return m_recordsPath.isolatedCopy();
}

size_t Storage::approximateSize() const
{
    return m_approximateRecordsSize + m_blobStorage.approximateSize();
}

uint32_t Storage::volumeBlockSize() const
{
    ASSERT(!RunLoop::isMain());

    if (!m_volumeBlockSize)
        m_volumeBlockSize = FileSystem::volumeFileBlockSize(m_basePath).value_or(4 * KB);

    return *m_volumeBlockSize;
}

size_t Storage::estimateRecordsSize(unsigned recordCount, unsigned blobCount) const
{
    auto inlineBodyCount = recordCount - std::min(blobCount, recordCount);
    auto headerSizes = recordCount * volumeBlockSize();
    auto inlineBodySizes = (maximumInlineBodySize() / 2) * inlineBodyCount;
    return headerSizes + inlineBodySizes;
}

void Storage::synchronize()
{
    ASSERT(RunLoop::isMain());

    if (m_synchronizationInProgress || m_shrinkInProgress)
        return;
    m_synchronizationInProgress = true;

    LOG(NetworkCacheStorage, "(NetworkProcess) synchronizing cache");

    backgroundIOQueue().dispatch([this, protectedThis = Ref { *this }] () mutable {
        auto recordFilter = makeUnique<ContentsFilter>();
        auto blobFilter = makeUnique<ContentsFilter>();

        // Most of the disk space usage is in blobs if we are using them. Approximate records file sizes to avoid expensive stat() calls.
        size_t recordsSize = 0;
        unsigned recordCount = 0;
        unsigned blobCount = 0;

        String anyType;
        traverseRecordsFiles(recordsPathIsolatedCopy(), anyType, [&](const String& fileName, const String& hashString, const String& type, bool isBlob, const String& recordDirectoryPath) {
            auto filePath = FileSystem::pathByAppendingComponent(recordDirectoryPath, fileName);

            Key::HashType hash;
            if (!Key::stringToHash(hashString, hash)) {
                FileSystem::deleteFile(filePath);
                return;
            }

            if (isBlob) {
                ++blobCount;
                blobFilter->add(hash);
                return;
            }

            ++recordCount;

            recordFilter->add(hash);
        });

        recordsSize = estimateRecordsSize(recordCount, blobCount);

        m_blobStorage.synchronize();

        deleteEmptyRecordsDirectories(recordsPathIsolatedCopy());

        LOG(NetworkCacheStorage, "(NetworkProcess) cache synchronization completed size=%zu recordCount=%u", recordsSize, recordCount);

        RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }, recordFilter = WTFMove(recordFilter), blobFilter = WTFMove(blobFilter), recordsSize]() mutable {
            for (auto& recordFilterKey : m_recordFilterHashesAddedDuringSynchronization)
                recordFilter->add(recordFilterKey);
            m_recordFilterHashesAddedDuringSynchronization.clear();

            for (auto& hash : m_blobFilterHashesAddedDuringSynchronization)
                blobFilter->add(hash);
            m_blobFilterHashesAddedDuringSynchronization.clear();

            m_recordFilter = WTFMove(recordFilter);
            m_blobFilter = WTFMove(blobFilter);
            m_approximateRecordsSize = recordsSize;
            m_synchronizationInProgress = false;
            if (m_mode == Mode::AvoidRandomness)
                dispatchPendingWriteOperations();
        });

    });
}

void Storage::addToRecordFilter(const Key& key)
{
    ASSERT(RunLoop::isMain());

    if (m_recordFilter)
        m_recordFilter->add(key.hash());

    // If we get new entries during filter synchronization take care to add them to the new filter as well.
    if (m_synchronizationInProgress)
        m_recordFilterHashesAddedDuringSynchronization.append(key.hash());
}

bool Storage::mayContain(const Key& key) const
{
    ASSERT(RunLoop::isMain());
    return !m_recordFilter || m_recordFilter->mayContain(key.hash());
}

bool Storage::mayContainBlob(const Key& key) const
{
    ASSERT(RunLoop::isMain());
    return !m_blobFilter || m_blobFilter->mayContain(key.hash());
}

String Storage::recordDirectoryPathForKey(const Key& key) const
{
    ASSERT(!key.type().isEmpty());
    return FileSystem::pathByAppendingComponent(FileSystem::pathByAppendingComponent(recordsPathIsolatedCopy(), key.partitionHashAsString()), key.type());
}

String Storage::recordPathForKey(const Key& key) const
{
    return FileSystem::pathByAppendingComponent(recordDirectoryPathForKey(key), key.hashAsString());
}

static String blobPathForRecordPath(const String& recordPath)
{
    return makeString(recordPath, blobSuffix);
}

String Storage::blobPathForKey(const Key& key) const
{
    return blobPathForRecordPath(recordPathForKey(key));
}

struct RecordMetaData {
    RecordMetaData() { }
    explicit RecordMetaData(const Key& key)
        : cacheStorageVersion(Storage::version)
        , key(key)
    { }

    unsigned cacheStorageVersion;
    Key key;
    WallTime timeStamp;
    SHA1::Digest headerHash;
    uint64_t headerSize { 0 };
    SHA1::Digest bodyHash;
    uint64_t bodySize { 0 };
    bool isBodyInline { false };

    // Not encoded as a field. Header starts immediately after meta data.
    uint64_t headerOffset { 0 };
};

static WARN_UNUSED_RETURN bool decodeRecordMetaData(RecordMetaData& metaData, const Data& fileData)
{
    bool success = false;
    fileData.apply([&metaData, &success](std::span<const uint8_t> span) {
        WTF::Persistence::Decoder decoder(span);
        
        std::optional<unsigned> cacheStorageVersion;
        decoder >> cacheStorageVersion;
        if (!cacheStorageVersion)
            return false;
        metaData.cacheStorageVersion = WTFMove(*cacheStorageVersion);

        std::optional<Key> key;
        decoder >> key;
        if (!key)
            return false;
        metaData.key = WTFMove(*key);

        std::optional<WallTime> timeStamp;
        decoder >> timeStamp;
        if (!timeStamp)
            return false;
        metaData.timeStamp = WTFMove(*timeStamp);

        std::optional<SHA1::Digest> headerHash;
        decoder >> headerHash;
        if (!headerHash)
            return false;
        metaData.headerHash = WTFMove(*headerHash);

        std::optional<uint64_t> headerSize;
        decoder >> headerSize;
        if (!headerSize)
            return false;
        metaData.headerSize = WTFMove(*headerSize);

        std::optional<SHA1::Digest> bodyHash;
        decoder >> bodyHash;
        if (!bodyHash)
            return false;
        metaData.bodyHash = WTFMove(*bodyHash);

        std::optional<uint64_t> bodySize;
        decoder >> bodySize;
        if (!bodySize)
            return false;
        metaData.bodySize = WTFMove(*bodySize);

        std::optional<bool> isBodyInline;
        decoder >> isBodyInline;
        if (!isBodyInline)
            return false;
        metaData.isBodyInline = WTFMove(*isBodyInline);

        if (!decoder.verifyChecksum())
            return false;

        metaData.headerOffset = decoder.currentOffset();
        success = true;
        return false;
    });
    return success;
}

static WARN_UNUSED_RETURN bool decodeRecordHeader(const Data& fileData, RecordMetaData& metaData, Data& headerData, const Salt& salt)
{
    if (!decodeRecordMetaData(metaData, fileData)) {
        LOG(NetworkCacheStorage, "(NetworkProcess) meta data decode failure");
        return false;
    }

    if (metaData.cacheStorageVersion != Storage::version) {
        LOG(NetworkCacheStorage, "(NetworkProcess) version mismatch");
        return false;
    }

    headerData = fileData.subrange(metaData.headerOffset, metaData.headerSize);
    if (metaData.headerHash != computeSHA1(headerData, salt)) {
        LOG(NetworkCacheStorage, "(NetworkProcess) header checksum mismatch");
        return false;
    }
    return true;
}

Storage::Record Storage::readRecord(const Data& recordData)
{
    ASSERT(!RunLoop::isMain());

    RecordMetaData metaData;
    Data headerData;
    if (!decodeRecordHeader(recordData, metaData, headerData, m_salt))
        return { };

    // Sanity check against time stamps in future.
    if (metaData.timeStamp > WallTime::now())
        return { };

    Data bodyData;
    if (metaData.isBodyInline) {
        size_t bodyOffset = metaData.headerOffset + headerData.size();
        if (bodyOffset + metaData.bodySize != recordData.size())
            return { };
        bodyData = recordData.subrange(bodyOffset, metaData.bodySize);
        if (metaData.bodyHash != computeSHA1(bodyData, m_salt))
            return { };
    }

    return Record {
        WTFMove(metaData.key),
        metaData.timeStamp,
        WTFMove(headerData),
        WTFMove(bodyData),
        metaData.bodyHash
    };
}

static Data encodeRecordMetaData(const RecordMetaData& metaData)
{
    WTF::Persistence::Encoder encoder;

    encoder << metaData.cacheStorageVersion;
    encoder << metaData.key;
    encoder << metaData.timeStamp;
    encoder << metaData.headerHash;
    encoder << metaData.headerSize;
    encoder << metaData.bodyHash;
    encoder << metaData.bodySize;
    encoder << metaData.isBodyInline;

    encoder.encodeChecksum();

    return Data(encoder.span());
}

std::optional<BlobStorage::Blob> Storage::storeBodyAsBlob(WriteOperationIdentifier identifier, const Storage::Record& record)
{
    auto blobPath = blobPathForKey(record.key);

    // Store the body.
    auto blob = m_blobStorage.add(blobPath, record.body);
    if (blob.data.isNull())
        return { };

    addWriteOperationActivity(identifier);

    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }, blob = WTFMove(blob), identifier] {
        assertIsMainThread();

        auto* writeOperation = m_activeWriteOperations.get(identifier);
        RELEASE_ASSERT(writeOperation);

        if (m_blobFilter)
            m_blobFilter->add(writeOperation->record().key.hash());

        if (m_synchronizationInProgress)
            m_blobFilterHashesAddedDuringSynchronization.append(writeOperation->record().key.hash());

        writeOperation->invokeMappedBodyHandler(blob.data);
        finishWriteOperationActivity(identifier);
    });
    return blob;
}

Data Storage::encodeRecord(const Record& record, std::optional<BlobStorage::Blob> blob)
{
    ASSERT(!blob || bytesEqual(blob.value().data, record.body));

    RecordMetaData metaData(record.key);
    metaData.timeStamp = record.timeStamp;
    metaData.headerHash = computeSHA1(record.header, m_salt);
    metaData.headerSize = record.header.size();
    metaData.bodyHash = blob ? blob.value().hash : computeSHA1(record.body, m_salt);
    metaData.bodySize = record.body.size();
    metaData.isBodyInline = !blob;

    auto encodedMetaData = encodeRecordMetaData(metaData);
    auto headerData = concatenate(encodedMetaData, record.header);

    if (metaData.isBodyInline)
        return concatenate(headerData, record.body);

    return { headerData };
}

void Storage::removeFromPendingWriteOperations(const Key& key)
{
    while (true) {
        auto found = m_pendingWriteOperations.findIf([&key](auto& operation) {
            assertIsMainThread();

            return operation->record().key == key;
        });

        if (found == m_pendingWriteOperations.end())
            break;

        m_pendingWriteOperations.remove(found);
    }
}

void Storage::remove(const Key& key)
{
    ASSERT(RunLoop::isMain());

    if (!mayContain(key))
        return;

    Ref protectedThis { *this };

    // We can't remove the key from the Bloom filter (but some false positives are expected anyway).
    // For simplicity we also don't reduce m_approximateSize on removals.
    // The next synchronization will update everything.

    removeFromPendingWriteOperations(key);

    serialBackgroundIOQueue().dispatch([this, protectedThis = Ref { *this }, key] () mutable {
        deleteFiles(key);
    });
}

void Storage::remove(const Vector<Key>& keys, CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());

    auto keysToRemove = WTF::compactMap(keys, [&](auto& key) -> std::optional<Key> {
        if (!mayContain(key))
            return std::nullopt;
        removeFromPendingWriteOperations(key);
        return key;
    });

    serialBackgroundIOQueue().dispatch([this, protectedThis = Ref { *this }, keysToRemove = WTFMove(keysToRemove), completionHandler = WTFMove(completionHandler)] () mutable {
        for (auto& key : keysToRemove)
            deleteFiles(key);

        RunLoop::mainSingleton().dispatch(WTFMove(completionHandler));
    });
}

void Storage::deleteFiles(const Key& key)
{
    ASSERT(!RunLoop::isMain());

    FileSystem::deleteFile(recordPathForKey(key));
    m_blobStorage.remove(blobPathForKey(key));
}

void Storage::updateFileModificationTime(String&& path)
{
    serialBackgroundIOQueue().dispatch([path = WTFMove(path).isolatedCopy()] {
        updateFileModificationTimeIfNeeded(path);
    });
}

void Storage::dispatchReadOperation(std::unique_ptr<ReadOperation> readOperationPtr)
{
    ASSERT(RunLoop::isMain());

    auto& readOperation = *readOperationPtr;
    auto identifier = readOperation.identifier();
    m_activeReadOperations.add(identifier, WTFMove(readOperationPtr));
    auto key = readOperation.key();
    auto recordPath = recordPathForKey(key);
    auto blobPath = mayContainBlob(key) ? blobPathForKey(key) : String { };

    readOperation.updateForDispatch(m_synchronizationInProgress, m_shrinkInProgress, m_readOperationDispatchCount);
    if (!blobPath.isEmpty())
        readOperation.setWaitsForBlob();

    ++m_readOperationDispatchCount;

    // Avoid randomness during testing.
    if (m_mode != Mode::AvoidRandomness) {
        // I/O pressure may make disk operations slow. If they start taking very long time we rather go to network.
        const Seconds readTimeout = 1500_ms;
        m_readOperationTimeoutTimer.startOneShot(readTimeout);
    }

    ioQueue().dispatch([this, protectedThis = Ref { *this }, identifier, recordPath = crossThreadCopy(WTFMove(recordPath)), blobPath = crossThreadCopy(WTFMove(blobPath))]() mutable {
        readRecordFromData(identifier, MonotonicTime::now(), FileSystem::readEntireFile(recordPath));
        readBlobIfNecessary(identifier, blobPath);
    });
}

void Storage::readRecordFromData(Storage::ReadOperationIdentifier identifier, MonotonicTime recordIOStartTime, std::optional<Vector<uint8_t>>&& data)
{
    Record record;
    if (data)
        record = readRecord(WTFMove(*data));

    auto recordIOEndTime = MonotonicTime::now();
    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }, identifier, recordIOStartTime, recordIOEndTime, record = crossThreadCopy(WTFMove(record))]() mutable {
        auto* readOperation = m_activeReadOperations.get(identifier);
        RELEASE_ASSERT(readOperation);

        readOperation->finishReadRecord(WTFMove(record), recordIOStartTime, recordIOEndTime);
        if (readOperation->canFinish())
            finishReadOperation(identifier);
    });
}

void Storage::readBlobIfNecessary(Storage::ReadOperationIdentifier identifier, const String& blobPath)
{
    if (blobPath.isEmpty())
        return;

    auto blobIOStartTime = MonotonicTime::now();
    auto blob = m_blobStorage.get(blobPath);
    auto blobIOEndTime = MonotonicTime::now();
    RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }, identifier, blob = WTFMove(blob), blobIOStartTime, blobIOEndTime]() mutable {
        auto* readOperation = m_activeReadOperations.get(identifier);
        RELEASE_ASSERT(readOperation);

        readOperation->finishReadBlob(WTFMove(blob), blobIOStartTime, blobIOEndTime);
        if (readOperation->canFinish())
            finishReadOperation(identifier);
    });
}

void Storage::finishReadOperation(Storage::ReadOperationIdentifier identifier)
{
    ASSERT(RunLoop::isMain());

    auto readOperation = m_activeReadOperations.take(identifier);
    RELEASE_ASSERT(readOperation);

    bool success = readOperation->finish();
    if (success)
        updateFileModificationTime(recordPathForKey(readOperation->key()));
    else if (!readOperation->isCanceled())
        remove(readOperation->key());

    if (m_activeReadOperations.isEmpty())
        m_readOperationTimeoutTimer.stop();

    dispatchPendingReadOperations();

    LOG(NetworkCacheStorage, "(NetworkProcess) read complete success=%d", success);
}

void Storage::cancelAllReadOperations()
{
    ASSERT(RunLoop::isMain());

    for (auto& readOperation : m_activeReadOperations.values())
        readOperation->cancel();

    size_t pendingCount = m_pendingReadOperations.size();
    UNUSED_PARAM(pendingCount);

    while (!m_pendingReadOperations.isEmpty())
        m_pendingReadOperations.dequeue()->cancel();

    LOG(NetworkCacheStorage, "(NetworkProcess) retrieve timeout, canceled %u active and %zu pending", m_activeReadOperations.size(), pendingCount);
}

void Storage::dispatchPendingReadOperations()
{
    ASSERT(RunLoop::isMain());

    const int maximumActiveReadOperationCount = 5;

    while (!m_pendingReadOperations.isEmpty()) {
        if (m_activeReadOperations.size() > maximumActiveReadOperationCount) {
            LOG(NetworkCacheStorage, "(NetworkProcess) limiting parallel retrieves");
            return;
        }
        dispatchReadOperation(m_pendingReadOperations.dequeue());
    }
}

template <class T> bool retrieveFromMemory(const T& operations, const Key& key, Storage::RetrieveCompletionHandler& completionHandler)
{
    assertIsMainThread();

    for (auto& operation : operations) {
        if (operation->record().key == key) {
            LOG(NetworkCacheStorage, "(NetworkProcess) found write operation in progress");
            RunLoop::mainSingleton().dispatch([record = operation->record(), completionHandler = WTFMove(completionHandler)] () mutable {
                completionHandler(WTFMove(record), { });
            });
            return true;
        }
    }
    return false;
}

void Storage::dispatchPendingWriteOperations()
{
    ASSERT(RunLoop::isMain());

    const int maximumActiveWriteOperationCount { 1 };

    while (!m_pendingWriteOperations.isEmpty()) {
        if (m_activeWriteOperations.size() >= maximumActiveWriteOperationCount) {
            LOG(NetworkCacheStorage, "(NetworkProcess) limiting parallel writes");
            return;
        }
        dispatchWriteOperation(m_pendingWriteOperations.takeLast());
    }
}

bool Storage::shouldStoreBodyAsBlob(const Data& bodyData)
{
    return bodyData.size() > maximumInlineBodySize();
}

void Storage::dispatchWriteOperation(std::unique_ptr<WriteOperation> writeOperationPtr)
{
    assertIsMainThread();

    auto& writeOperation = *writeOperationPtr;
    auto identifier = writeOperation.identifier();
    m_activeWriteOperations.add(identifier, WTFMove(writeOperationPtr));

    auto record = writeOperation.record();
    // This was added already when starting the store but filter might have been wiped.
    addToRecordFilter(record.key);

    backgroundIOQueue().dispatch([this, protectedThis = Ref { *this }, identifier, record = crossThreadCopy(WTFMove(record))]() mutable {
        auto recordDirectoryPath = recordDirectoryPathForKey(record.key);
        auto recordPath = recordPathForKey(record.key);
        FileSystem::makeAllDirectories(recordDirectoryPath);

        addWriteOperationActivity(identifier);

        bool shouldStoreAsBlob = shouldStoreBodyAsBlob(record.body);
        auto blob = shouldStoreAsBlob ? storeBodyAsBlob(identifier, record) : std::nullopt;
        auto recordData = encodeRecord(record, blob);
        auto recordSize = recordData.size();

        if (!FileSystem::overwriteEntireFile(recordPath, recordData.span()))
            RELEASE_LOG_ERROR(NetworkCacheStorage, "Failed to write %zu bytes of network cache record data to %" PUBLIC_LOG_STRING, recordSize, recordPath.utf8().data());

        RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }, identifier, recordSize]() mutable {
            m_approximateRecordsSize += recordSize;
            finishWriteOperationActivity(identifier);
        });
    });
}

void Storage::addWriteOperationActivity(WriteOperationIdentifier identifier)
{
    Locker locker { m_activitiesLock };
    m_writeOperationActivities.add(identifier);
}

bool Storage::removeWriteOperationActivity(WriteOperationIdentifier identifier)
{
    Locker locker { m_activitiesLock };
    ASSERT(m_writeOperationActivities.contains(identifier));
    return m_writeOperationActivities.remove(identifier);
}

void Storage::finishWriteOperationActivity(WriteOperationIdentifier identifier)
{
    ASSERT(RunLoop::isMain());
    if (!removeWriteOperationActivity(identifier))
        return;

    auto writeOperation = m_activeWriteOperations.take(identifier);
    RELEASE_ASSERT(writeOperation);

    dispatchPendingWriteOperations();

    shrinkIfNeeded();
}

void Storage::retrieve(const Key& key, unsigned priority, RetrieveCompletionHandler&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    ASSERT(!key.isNull());

    if (!m_capacity) {
        completionHandler({ }, { });
        return;
    }

    if (!mayContain(key)) {
        completionHandler({ }, { });
        return;
    }

    if (retrieveFromMemory(m_pendingWriteOperations, key, completionHandler))
        return;

    if (retrieveFromMemory(m_activeWriteOperations.values(), key, completionHandler))
        return;

    auto readOperation = makeUnique<ReadOperation>(key, priority, WTFMove(completionHandler));
    readOperation->updateForStart(m_readOperationDispatchCount);

    m_pendingReadOperations.enqueue(WTFMove(readOperation));
    dispatchPendingReadOperations();
}

void Storage::store(const Record& record, MappedBodyHandler&& mappedBodyHandler)
{
    ASSERT(RunLoop::isMain());
    ASSERT(!record.key.isNull());

    if (!m_capacity)
        return;

    auto writeOperation = makeUnique<WriteOperation>(record, WTFMove(mappedBodyHandler));
    m_pendingWriteOperations.prepend(WTFMove(writeOperation));

    // Add key to the filter already here as we do lookups from the pending operations too.
    addToRecordFilter(record.key);

    bool isInitialWrite = m_pendingWriteOperations.size() == 1;
    if (!isInitialWrite || (m_synchronizationInProgress && m_mode == Mode::AvoidRandomness))
        return;

    m_writeOperationDispatchTimer.startOneShot(m_initialWriteDelay);
}

void Storage::traverseWithinRootPath(const String& rootPath, const String& type, OptionSet<TraverseFlag> flags, TraverseHandler&& traverseHandler)
{
    ASSERT(RunLoop::isMain());
    ASSERT(traverseHandler);

    auto traverseOperation = TraverseOperation::create(WTFMove(traverseHandler));
    ioQueue().dispatch([this, protectedThis = Ref { *this }, traverseOperation = WTFMove(traverseOperation), flags, rootPath = crossThreadCopy(rootPath), type = crossThreadCopy(type)]() mutable {
        traverseRecordsFiles(rootPath, type, [this, protectedThis, expectedType = type, flags, traverseOperation](const String& fileName, const String& hashString, const String& type, bool isBlob, const String& recordDirectoryPath) {
            ASSERT(type == expectedType || expectedType.isEmpty());
            if (isBlob)
                return;

            auto recordPath = FileSystem::pathByAppendingComponent(recordDirectoryPath, fileName);
            double worth = -1;
            if (flags & TraverseFlag::ComputeWorth)
                worth = computeRecordWorth(fileTimes(recordPath));

            unsigned bodyShareCount = 0;
            if (flags & TraverseFlag::ShareCount)
                bodyShareCount = m_blobStorage.shareCount(blobPathForRecordPath(recordPath));

            traverseOperation->waitAndIncrementActivityCount();
            auto channel = IOChannel::open(WTFMove(recordPath), IOChannel::Type::Read);
            channel->read(0, std::numeric_limits<size_t>::max(), WorkQueue::mainSingleton(), [this, protectedThis, traverseOperation, worth, bodyShareCount](auto fileData, int) {
                RecordMetaData metaData;
                Data headerData;
                if (decodeRecordHeader(fileData, metaData, headerData, m_salt)) {
                    Record record {
                        metaData.key,
                        metaData.timeStamp,
                        headerData,
                        { },
                        metaData.bodyHash
                    };
                    RecordInfo info {
                        static_cast<size_t>(metaData.bodySize),
                        worth,
                        bodyShareCount,
                        String::fromUTF8(SHA1::hexDigest(metaData.bodyHash).span())
                    };
                    traverseOperation->invokeHandler(&record, info);
                }
                traverseOperation->decrementActivityCount();
            });
        });

        traverseOperation->waitUntilActivitiesFinished();
        RunLoop::mainSingleton().dispatch([traverseOperation = WTFMove(traverseOperation)]() mutable {
            // Invoke with nullptr to indicate this is the last record.
            traverseOperation->invokeHandler(nullptr, { });
        });
    });
}

void Storage::traverse(const String& type, OptionSet<TraverseFlag> flags, TraverseHandler&& traverseHandler)
{
    traverseWithinRootPath(recordsPathIsolatedCopy(), type, flags, WTFMove(traverseHandler));
}

void Storage::traverse(const String& type, const String& partition, OptionSet<TraverseFlag> flags, TraverseHandler&& traverseHandler)
{
    auto partitionHashAsString = Key::partitionToPartitionHashAsString(partition, salt());
    auto rootPath = FileSystem::pathByAppendingComponent(recordsPathIsolatedCopy(), partitionHashAsString);
    traverseWithinRootPath(rootPath, type, flags, WTFMove(traverseHandler));
}

void Storage::setCapacity(size_t capacity)
{
    ASSERT(RunLoop::isMain());
    if (m_capacity == capacity)
        return;

#if ASSERT_ENABLED
    const size_t assumedAverageRecordSize = 50 << 10;
    size_t maximumRecordCount = capacity / assumedAverageRecordSize;
    // ~10 bits per element are required for <1% false positive rate.
    size_t effectiveBloomFilterCapacity = ContentsFilter::tableSize / 10;
    // If this gets hit it might be time to increase the filter size.
    ASSERT(maximumRecordCount < effectiveBloomFilterCapacity);
#endif

    m_capacity = capacity;

    shrinkIfNeeded();
}

void Storage::clear(String&& type, WallTime modifiedSinceTime, CompletionHandler<void()>&& completionHandler)
{
    ASSERT(RunLoop::isMain());
    LOG(NetworkCacheStorage, "(NetworkProcess) clearing cache");

    if (m_recordFilter)
        m_recordFilter->clear();
    if (m_blobFilter)
        m_blobFilter->clear();
    m_approximateRecordsSize = 0;

    ioQueue().dispatch([this, protectedThis = Ref { *this }, modifiedSinceTime, completionHandler = WTFMove(completionHandler), type = WTFMove(type).isolatedCopy()] () mutable {
        auto recordsPath = this->recordsPathIsolatedCopy();
        traverseRecordsFiles(recordsPath, type, [modifiedSinceTime](const String& fileName, const String& hashString, const String& type, bool isBlob, const String& recordDirectoryPath) {
            auto filePath = FileSystem::pathByAppendingComponent(recordDirectoryPath, fileName);
            if (modifiedSinceTime > -WallTime::infinity()) {
                auto times = fileTimes(filePath);
                if (times.modification < modifiedSinceTime)
                    return;
            }
            FileSystem::deleteFile(filePath);
        });

        deleteEmptyRecordsDirectories(recordsPath);

        // This cleans unreferenced blobs.
        m_blobStorage.synchronize();

        RunLoop::mainSingleton().dispatch(WTFMove(completionHandler));
    });
}

static double computeRecordWorth(FileTimes times)
{
    auto age = WallTime::now() - times.creation;
    // File modification time is updated manually on cache read. We don't use access time since OS may update it automatically.
    auto accessAge = times.modification - times.creation;

    // For sanity.
    if (age <= 0_s || accessAge < 0_s || accessAge > age)
        return 0;

    // We like old entries that have been accessed recently.
    return accessAge / age;
}

static double deletionProbability(FileTimes times, unsigned bodyShareCount)
{
    static const double maximumProbability { 0.33 };
    static const unsigned maximumEffectiveShareCount { 5 };

    auto worth = computeRecordWorth(times);

    // Adjust a bit so the most valuable entries don't get deleted at all.
    auto effectiveWorth = std::min(1.1 * worth, 1.);

    auto probability =  (1 - effectiveWorth) * maximumProbability;

    // It is less useful to remove an entry that shares its body data.
    if (bodyShareCount)
        probability /= std::min(bodyShareCount, maximumEffectiveShareCount);

    return probability;
}

void Storage::shrinkIfNeeded()
{
    ASSERT(RunLoop::isMain());

    // Avoid randomness caused by cache shrinks.
    if (m_mode == Mode::AvoidRandomness)
        return;

    if (approximateSize() > m_capacity)
        shrink();
}

void Storage::shrink()
{
    ASSERT(RunLoop::isMain());

    if (m_shrinkInProgress || m_synchronizationInProgress)
        return;
    m_shrinkInProgress = true;

    LOG(NetworkCacheStorage, "(NetworkProcess) shrinking cache approximateSize=%zu capacity=%zu", approximateSize(), m_capacity);

    backgroundIOQueue().dispatch([this, protectedThis = Ref { *this }] () mutable {
        auto recordsPath = this->recordsPathIsolatedCopy();
        String anyType;
        traverseRecordsFiles(recordsPath, anyType, [this](const String& fileName, const String& hashString, const String& type, bool isBlob, const String& recordDirectoryPath) {
            if (isBlob)
                return;

            auto recordPath = FileSystem::pathByAppendingComponent(recordDirectoryPath, fileName);
            auto blobPath = blobPathForRecordPath(recordPath);

            auto times = fileTimes(recordPath);
            unsigned bodyShareCount = m_blobStorage.shareCount(blobPath);
            auto probability = deletionProbability(times, bodyShareCount);

            bool shouldDelete = cryptographicallyRandomUnitInterval() < probability;

            LOG(NetworkCacheStorage, "Deletion probability=%f bodyLinkCount=%d shouldDelete=%d", probability, bodyShareCount, shouldDelete);

            if (shouldDelete) {
                FileSystem::deleteFile(recordPath);
                m_blobStorage.remove(blobPath);
            }
        });

        RunLoop::mainSingleton().dispatch([this, protectedThis = Ref { *this }] {
            m_shrinkInProgress = false;
            // We could synchronize during the shrink traversal. However this is fast and it is better to have just one code path.
            synchronize();
        });

        LOG(NetworkCacheStorage, "(NetworkProcess) cache shrink completed");
    });
}

void Storage::deleteOldVersions()
{
    backgroundIOQueue().dispatch([cachePath = basePathIsolatedCopy()] () mutable {
        traverseDirectory(cachePath, [&cachePath](const String& subdirName, DirectoryEntryType type) {
            if (type != DirectoryEntryType::Directory)
                return;
            if (!subdirName.startsWith(versionDirectoryPrefix))
                return;
            auto directoryVersion = parseInteger<unsigned>(StringView { subdirName }.substring(versionDirectoryPrefix.length()));
            if (!directoryVersion || *directoryVersion >= version)
                return;
            auto oldVersionPath = FileSystem::pathByAppendingComponent(cachePath, subdirName);
            LOG(NetworkCacheStorage, "(NetworkProcess) deleting old cache version, path %s", oldVersionPath.utf8().data());
            FileSystem::deleteNonEmptyDirectory(oldVersionPath);
        });
    });
}

}
}
