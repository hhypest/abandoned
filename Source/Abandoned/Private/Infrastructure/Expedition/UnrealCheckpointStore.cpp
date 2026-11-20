#include "Infrastructure/Expedition/UnrealEntryCourse.h"

#include "Algo/Find.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Infrastructure/Expedition/EntryCourseSave.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Windows/WindowsHWrapper.h"

DEFINE_LOG_CATEGORY_STATIC(LogCampaignCheckpoint, Log, All);

namespace Abandoned::Infrastructure::Expedition
{
namespace
{
using Application::Expedition::Checkpoint;
using Application::Expedition::CheckpointIssue;
using Domain::Expedition::Segment;
using Domain::Expedition::SuitRecord;

// Accessed on the game thread. The worker only owns the immutable write and its Win32 lock.
TSharedFuture<bool> PendingPublication;

bool ReportWrite(bool Succeeded, bool IsArrival)
{
    if (!Succeeded && IsArrival)
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Could not save the entry checkpoint. Previous data preserved."));
    }
    return Succeeded;
}

void ReleaseWriter(HANDLE Lock)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Unlock);
    ::CloseHandle(Lock);
}

constexpr int32 CampaignVersion = 2;
constexpr int64 MaxCheckpointBytes = 1024 * 1024;
constexpr const TCHAR* LegacySlot = TEXT("AbandonedEntryCourse");
constexpr const TCHAR* CampaignFile = TEXT("AbandonedCampaign.v2.json");

struct ArchiveKey
{
    SuitRecord Record;
    const TCHAR* Key;
};

constexpr ArchiveKey ArchiveKeys[] = {
    {SuitRecord::ForecastSynchronized, TEXT("forecast-synchronized")},
    {SuitRecord::Decontaminated, TEXT("decontaminated")},
    {SuitRecord::OxygenRestored, TEXT("oxygen-restored")}
};

FString SaveDirectory()
{
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames")));
}

FString NativeFilename(FString Name)
{
    FPaths::MakePlatformFilename(Name);
    return Name;
}

Checkpoint Issue(CheckpointIssue Problem)
{
    return {std::nullopt, Problem, {}};
}

Checkpoint ReadLegacy(IPlatformFile& Files, const FString& Directory)
{
    const FString Path = FPaths::Combine(Directory, TEXT("AbandonedEntryCourse.sav"));
    if (!Files.FileExists(*Path))
    {
        return {};
    }
    const int64 Size = Files.FileSize(*Path);
    if (Size < 4 || Size > MaxCheckpointBytes)
    {
        return Issue(CheckpointIssue::Unreadable);
    }
    const auto* Save = Cast<UEntryCourseSave>(UGameplayStatics::LoadGameFromSlot(LegacySlot, 0));
    if (!Save)
    {
        return Issue(CheckpointIssue::Unreadable);
    }
    if (Save->Version != UEntryCourseSave::CurrentVersion)
    {
        return Issue(CheckpointIssue::UnsupportedVersion);
    }
    const auto Chamber = ReadSegment(Save->Chamber);
    return Chamber && Domain::Expedition::IsCheckpoint(*Chamber)
        ? Checkpoint{Chamber, CheckpointIssue::None, {}} : Issue(CheckpointIssue::UnknownCheckpoint);
}

Checkpoint ReadCampaign(IPlatformFile& Files, const FString& Path)
{
    const int64 Size = Files.FileSize(*Path);
    FString Text;
    TSharedPtr<FJsonObject> Object;
    if (Size <= 0 || Size > MaxCheckpointBytes || !FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object)
    {
        return Issue(CheckpointIssue::Unreadable);
    }
    double Version = 0.0;
    if (!Object->HasTypedField<EJson::Number>(TEXT("version")) || !Object->TryGetNumberField(TEXT("version"), Version))
    {
        return Issue(CheckpointIssue::Unreadable);
    }
    if (Version != CampaignVersion)
    {
        return Issue(CheckpointIssue::UnsupportedVersion);
    }
    FString Id;
    const auto Chamber = Object->TryGetStringField(TEXT("checkpoint"), Id) ? ReadSegment(Id) : std::nullopt;
    if (!Chamber || !Domain::Expedition::IsCheckpoint(*Chamber))
    {
        return Issue(CheckpointIssue::UnknownCheckpoint);
    }
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Object->TryGetArrayField(TEXT("archive"), Records) || Records->Num() > UE_ARRAY_COUNT(ArchiveKeys))
    {
        return Issue(CheckpointIssue::InvalidArchive);
    }
    Checkpoint Result{Chamber, CheckpointIssue::None, {}};
    for (const TSharedPtr<FJsonValue>& Value : *Records)
    {
        FString Key;
        if (!Value || !Value->TryGetString(Key))
        {
            return Issue(CheckpointIssue::InvalidArchive);
        }
        const ArchiveKey* Match = Algo::FindByPredicate(ArchiveKeys,
            [&Key](const ArchiveKey& Entry) { return Key.Equals(Entry.Key, ESearchCase::CaseSensitive); });
        if (!Match)
        {
            return Issue(CheckpointIssue::InvalidArchive);
        }
        Result.Archive.Reveal(Match->Record);
    }
    if (Result.Archive.Size() != static_cast<std::size_t>(Records->Num()))
    {
        return Issue(CheckpointIssue::InvalidArchive);
    }
    return Result;
}

bool Encode(Segment Chamber, const Domain::Expedition::SuitArchive& Archive, FString& Text)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Encode);
    auto Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("version"), CampaignVersion);
    Object->SetStringField(TEXT("checkpoint"), WriteSegment(Chamber));
    TArray<TSharedPtr<FJsonValue>> Records;
    for (std::size_t Index = 0; Index < Archive.Size(); ++Index)
    {
        const auto Record = Archive.At(Index);
        const ArchiveKey* Match = Record ? Algo::FindBy(ArchiveKeys, *Record, &ArchiveKey::Record) : nullptr;
        if (!Match)
        {
            return false;
        }
        Records.Add(MakeShared<FJsonValueString>(Match->Key));
    }
    Object->SetArrayField(TEXT("archive"), MoveTemp(Records));
    return FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
}

bool Publish(IPlatformFile& Files, const FString& Path, const FString& Text, bool ReplaceExisting)
{

    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    ON_SCOPE_EXIT
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Cleanup);
        Files.DeleteFile(*Temporary);
    };
    const FTCHARToUTF8 Bytes(*Text);
    TUniquePtr<IFileHandle> Handle;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_TempOpen);
        Handle.Reset(Files.OpenWrite(*Temporary));
    }
    if (!Handle)
    {
        return false;
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_TempWrite);
        if (!Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()))
        {
            return false;
        }
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Flush);
        if (!Handle->Flush(true))
        {
            UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Could not flush the temporary campaign checkpoint."));
            return false;
        }
    }
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_TempClose);
        Handle.Reset();
    }

    // Same-directory rename without a copy fallback. The current slot is never deleted first.
    const FString Source = NativeFilename(Temporary);
    const FString Destination = NativeFilename(Path);
    const DWORD Flags = MOVEFILE_WRITE_THROUGH | (ReplaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
    TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Replace);
    if (!::MoveFileExW(*Source, *Destination, Flags))
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Campaign checkpoint replacement failed (Win32 error %lu)."), ::GetLastError());
        return false;
    }
    return true;
}
}

const TCHAR* CheckpointIssueText(CheckpointIssue Problem)
{
    switch (Problem)
    {
    case CheckpointIssue::Unreadable: return TEXT("Checkpoint data could not be read.");
    case CheckpointIssue::UnsupportedVersion: return TEXT("Unsupported checkpoint version.");
    case CheckpointIssue::UnknownCheckpoint: return TEXT("Unknown checkpoint ID.");
    case CheckpointIssue::InvalidArchive: return TEXT("Invalid checkpoint archive.");
    default: return TEXT("");
    }
}

void UnrealCheckpointStore::WaitForPublication()
{
    check(IsInGameThread());
    if (PendingPublication.IsValid() && !PendingPublication.IsReady())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_WaitForPublication);
        PendingPublication.Wait();
    }
    // Release the future and its pooled event before the engine tears down the event pool.
    PendingPublication = {};
}

Checkpoint UnrealCheckpointStore::Read() const
{
    WaitForPublication();
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Directory = SaveDirectory();
    const FString Path = FPaths::Combine(Directory, CampaignFile);
    const bool HasCampaign = Files.FileExists(*Path);
    const Checkpoint Result = HasCampaign ? ReadCampaign(Files, Path) : ReadLegacy(Files, Directory);
    if (Result.Chamber)
    {
        UE_LOG(LogCampaignCheckpoint, Display, TEXT("Loaded campaign checkpoint v%d: %s."),
            HasCampaign ? CampaignVersion : UEntryCourseSave::CurrentVersion, *WriteSegment(*Result.Chamber));
    }
    return Result;
}

bool UnrealCheckpointStore::Write(Segment Chamber) const
{
    return BeginArrival(Chamber).Get();
}

TSharedFuture<bool> UnrealCheckpointStore::BeginArrival(Segment Chamber) const
{
    return Domain::Expedition::IsCheckpoint(Chamber)
        ? Update(Chamber, std::nullopt)
        : MakeFulfilledPromise<bool>(false).GetFuture().Share();
}

bool UnrealCheckpointStore::RecordResupply() const
{
    return Update(std::nullopt, SuitRecord::OxygenRestored).Get();
}

TSharedFuture<bool> UnrealCheckpointStore::Update(std::optional<Segment> Chamber, std::optional<SuitRecord> Record) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Update);
    WaitForPublication();
    const auto Completed = [IsArrival = Chamber.has_value()](bool Succeeded)
    {
        return MakeFulfilledPromise<bool>(ReportWrite(Succeeded, IsArrival)).GetFuture().Share();
    };
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Directory = SaveDirectory();
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Directory);
        if (!Files.CreateDirectoryTree(*Directory))
        {
            return Completed(false);
        }
    }
    const FString Path = FPaths::Combine(Directory, CampaignFile);
    const FString LockPath = NativeFilename(Path + TEXT(".lock"));
    HANDLE Lock;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Lock);
        Lock = ::CreateFileW(*LockPath, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (Lock == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Could not acquire the campaign checkpoint writer lock."));
        return Completed(false);
    }
    ON_SCOPE_EXIT
    {
        if (Lock != INVALID_HANDLE_VALUE)
        {
            ReleaseWriter(Lock);
        }
    };

    bool HasCampaign;
    Checkpoint Existing;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Read);
        HasCampaign = Files.FileExists(*Path);
        Existing = HasCampaign ? ReadCampaign(Files, Path) : ReadLegacy(Files, Directory);
    }
    if (Existing.Issue != CheckpointIssue::None)
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Campaign checkpoint preserved. %s"), CheckpointIssueText(Existing.Issue));
        return Completed(false);
    }
    const auto Next = Chamber ? Chamber : Existing.Chamber;
    if (!Next)
    {
        return Completed(false);
    }
    const std::size_t OldRecordCount = Existing.Archive.Size();
    if (Record)
    {
        Existing.Archive.Reveal(*Record);
    }
    if (HasCampaign && Next == Existing.Chamber && Existing.Archive.Size() == OldRecordCount)
    {
        return Completed(true);
    }
    FString Text;
    if (!Encode(*Next, Existing.Archive, Text))
    {
        return Completed(false);
    }
    const FString SegmentId = WriteSegment(*Next);
    const int32 RecordCount = static_cast<int32>(Existing.Archive.Size());
    PendingPublication = Async(EAsyncExecution::ThreadPool,
        [Path, Text = MoveTemp(Text), HasCampaign, SegmentId, RecordCount, IsArrival = Chamber.has_value(), Lock]
        {
            TRACE_CPUPROFILER_EVENT_SCOPE(Checkpoint_Publish);
            ON_SCOPE_EXIT { ReleaseWriter(Lock); };
            if (!Publish(FPlatformFileManager::Get().GetPlatformFile(), Path, Text, HasCampaign))
            {
                return ReportWrite(false, IsArrival);
            }
            UE_LOG(LogCampaignCheckpoint, Display, TEXT("Saved campaign checkpoint v2: %s (%d archive records)."),
                *SegmentId, RecordCount);
            return true;
        }).Share();
    // Hand the exclusive handle to the worker until the same-directory replacement completes.
    Lock = INVALID_HANDLE_VALUE;
    return PendingPublication;
}
}
