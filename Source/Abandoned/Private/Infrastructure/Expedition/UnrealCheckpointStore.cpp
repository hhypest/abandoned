#include "Infrastructure/Expedition/UnrealEntryCourse.h"

#include "Algo/Find.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Infrastructure/Expedition/EntryCourseSave.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
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

bool Publish(IPlatformFile& Files, const FString& Path, Segment Chamber,
    const Domain::Expedition::SuitArchive& Archive, bool ReplaceExisting)
{
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
    FString Text;
    if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)))
    {
        return false;
    }

    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    ON_SCOPE_EXIT { Files.DeleteFile(*Temporary); };
    {
        const FTCHARToUTF8 Bytes(*Text);
        TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Temporary));
        if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) || !Handle->Flush(true))
        {
            UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Could not write or flush the temporary campaign checkpoint."));
            return false;
        }
    }

    // Same-directory rename without a copy fallback. The current slot is never deleted first.
    const FString Source = NativeFilename(Temporary);
    const FString Destination = NativeFilename(Path);
    const DWORD Flags = MOVEFILE_WRITE_THROUGH | (ReplaceExisting ? MOVEFILE_REPLACE_EXISTING : 0);
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

Checkpoint UnrealCheckpointStore::Read() const
{
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
    return Domain::Expedition::IsCheckpoint(Chamber) && Update(Chamber, std::nullopt);
}

bool UnrealCheckpointStore::RecordResupply() const
{
    return Update(std::nullopt, SuitRecord::OxygenRestored);
}

bool UnrealCheckpointStore::Update(std::optional<Segment> Chamber, std::optional<SuitRecord> Record) const
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const FString Directory = SaveDirectory();
    if (!Files.CreateDirectoryTree(*Directory))
    {
        return false;
    }
    const FString Path = FPaths::Combine(Directory, CampaignFile);
    const FString LockPath = NativeFilename(Path + TEXT(".lock"));
    const HANDLE Lock = ::CreateFileW(*LockPath, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Lock == INVALID_HANDLE_VALUE)
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Could not acquire the campaign checkpoint writer lock."));
        return false;
    }
    ON_SCOPE_EXIT { ::CloseHandle(Lock); };

    const bool HasCampaign = Files.FileExists(*Path);
    Checkpoint Existing = HasCampaign ? ReadCampaign(Files, Path) : ReadLegacy(Files, Directory);
    if (Existing.Issue != CheckpointIssue::None)
    {
        UE_LOG(LogCampaignCheckpoint, Warning, TEXT("Campaign checkpoint preserved. %s"), CheckpointIssueText(Existing.Issue));
        return false;
    }
    const auto Next = Chamber ? Chamber : Existing.Chamber;
    if (!Next)
    {
        return false;
    }
    const std::size_t OldRecordCount = Existing.Archive.Size();
    if (Record)
    {
        Existing.Archive.Reveal(*Record);
    }
    if (HasCampaign && Next == Existing.Chamber && Existing.Archive.Size() == OldRecordCount)
    {
        return true;
    }
    if (!Publish(Files, Path, *Next, Existing.Archive, HasCampaign))
    {
        return false;
    }
    UE_LOG(LogCampaignCheckpoint, Display, TEXT("Saved campaign checkpoint v2: %s (%d archive records)."),
        *WriteSegment(*Next), static_cast<int32>(Existing.Archive.Size()));
    return true;
}
}
