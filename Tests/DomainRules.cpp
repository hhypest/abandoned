#include "Application/Expedition/EntryCourse.h"
#include "Application/Expedition/Dev20Transfer.h"
#include "Application/Expedition/MantleRequest.h"
#include "Application/Expedition/PassageHazards.h"
#include "Application/Expedition/SuitSession.h"
#include "Application/Expedition/SuitTerminal.h"
#include "Application/Puzzles/OpticsCourse.h"
#include "Application/Puzzles/TimedPassage.h"
#include "Application/Puzzles/ChamberHintReader.h"
#include "Domain/Expedition/FallImpact.h"
#include "Domain/Expedition/RadiationExposure.h"
#include "Domain/Expedition/SuitSupply.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Domain/Puzzles/ChamberCircuit.h"
#include "Domain/Puzzles/GravityField.h"
#include "Domain/Puzzles/OpticalReflector.h"
#include "Domain/Puzzles/PortalTransit.h"
#include "Domain/Puzzles/PlateBalance.h"
#include "Domain/Puzzles/SlidingDoorRules.h"
#include "Infrastructure/Expedition/WorldRouteOptions.h"

#include "TestChecks.h"
#include <limits>

using namespace Abandoned::Domain::Expedition;
using namespace Abandoned::Domain::Puzzles;
using namespace Abandoned::Application::Expedition;

namespace
{
struct MemoryCheckpoint final : CheckpointReader
{
    Checkpoint Data;
    mutable int Reads = 0;

    Checkpoint Read() const override
    {
        ++Reads;
        return Data;
    }
};

void TestRouteAndCheckpoint()
{
    static_assert(NextSegment(Segment::Stairs) == Segment::Button);
    static_assert(NextSegment(Segment::Button) == Segment::Weight);
    static_assert(NextSegment(Segment::Weight) == Segment::Balance);
    static_assert(NextSegment(Segment::Balance) == Segment::Passage);
    static_assert(NextSegment(Segment::Passage) == Segment::Vanguard);
    static_assert(!NextSegment(Segment::Vanguard));
    static_assert(!NextSegment(Segment::Sandbox));
    static_assert(!ParseSegment("Unknown"));
    static_assert(ParseSegment(SegmentKey(Segment::Weight)) == Segment::Weight);

    MemoryCheckpoint Store;
    CHECK(StartCourse({}, &Store).Current == Segment::Stairs);
    CHECK(RequiresArrivalCheckpoint({Segment::Stairs}));
    CHECK(RequiresArrivalCheckpoint({Segment::Button}));
    CHECK(RequiresArrivalCheckpoint({Segment::Weight}));
    CHECK(!RequiresArrivalCheckpoint({Segment::Passage}));
    CHECK(!RequiresArrivalCheckpoint({Segment::Vanguard}));
    Store.Data = {Segment::Weight};
    CHECK(StartCourse({}, &Store).Current == Segment::Weight);
    CHECK(StartCourse({Segment::Button}, &Store).Current == Segment::Button);

    const CourseStart Standalone = StartCourse({Segment::Balance}, &Store);
    CHECK(Standalone.StandaloneBalance && !NextCourseDestination(Standalone));
    CHECK(!RequiresArrivalCheckpoint(Standalone));
    CHECK(StartCourse({}, &Store).Current == Segment::Weight);

    const WorldDestination Passage = VanguardDestination{VanguardStop::Passage, RouteEntry::Campaign, Segment::Balance};
    const CourseStart Campaign = StartCourse({Segment::Balance, RouteEntry::Campaign}, &Store);
    CHECK(!Campaign.StandaloneBalance && NextCourseDestination(Campaign) == Passage);
    CHECK(RequiresArrivalCheckpoint(Campaign));
    Store.Data = {Campaign.Current};
    const CourseStart Resumed = StartCourse({}, &Store);
    CHECK(Resumed.Current == Segment::Balance && NextCourseDestination(Resumed) == Passage);

    Store.Data = {Segment::Vanguard};
    const CourseStart Recovered = StartCourse({}, &Store);
    CHECK(Recovered.Issue == CheckpointIssue::UnknownCheckpoint && Recovered.Current == Segment::Stairs);
    CHECK(!RequiresArrivalCheckpoint(Recovered));
}

void TestPassageRecovery()
{
    MemoryCheckpoint Store;
    const VanguardDestination Standard{};
    const VanguardDestination Campaign{VanguardStop::Passage, RouteEntry::Campaign, Segment::Balance};
    const VanguardDestination Radiation{VanguardStop::RadiationPassage};
    const WorldDestination ToWeight = ChamberDestination{Segment::Weight, RouteEntry::Campaign};
    const WorldDestination ToButton = ChamberDestination{Segment::Button, RouteEntry::Campaign};
    const WorldDestination ToBalance = ChamberDestination{Segment::Balance, RouteEntry::Campaign};

    Store.Data = {Segment::Button};
    CHECK(PassageRecovery(Campaign, &Store) == ToBalance);
    CHECK(PassageRecovery(Radiation, &Store) == WorldDestination{Radiation});
    CHECK(Store.Reads == 0);
    CHECK(PassageRecovery(Standard, &Store) == ToButton);
    CHECK((PassageRecovery({VanguardStop::Passage, RouteEntry::Direct, Segment::Sandbox}, &Store) == ToButton));
    CHECK(PassageRecovery(Standard, nullptr) == ToWeight);
    Store.Data = {};
    CHECK(PassageRecovery(Standard, &Store) == ToWeight);
    Store.Data = {Segment::Vanguard};
    CHECK(PassageRecovery(Standard, &Store) == ToWeight);
    Store.Data = {Segment::Button, CheckpointIssue::UnsupportedVersion};
    CHECK(PassageRecovery(Standard, &Store) == ToWeight);
    CHECK((StationAfterPassage(Campaign, ToBalance) ==
        VanguardDestination{VanguardStop::Station, RouteEntry::Campaign, Segment::Balance}));
    CHECK(StationAfterPassage(Radiation, Radiation) == VanguardDestination{VanguardStop::Station});
    CHECK(StationAfterPassage(Standard, ToWeight) == VanguardDestination{VanguardStop::Station});
}

void TestWorldRouteOptions()
{
    using namespace Abandoned::Infrastructure::Expedition;
    const WorldDestination Routes[] = {
        ChamberDestination{}, ChamberDestination{Segment::Stairs}, ChamberDestination{Segment::Button, RouteEntry::Campaign},
        ChamberDestination{Segment::Weight, RouteEntry::Campaign}, ChamberDestination{Segment::Balance},
        ChamberDestination{Segment::Balance, RouteEntry::Campaign}, ChamberDestination{Segment::Sandbox},
        VanguardDestination{}, VanguardDestination{VanguardStop::Station},
        VanguardDestination{VanguardStop::Passage, RouteEntry::Campaign, Segment::Balance},
        VanguardDestination{VanguardStop::Station, RouteEntry::Campaign, Segment::Balance},
        VanguardDestination{VanguardStop::RadiationPassage},
        Dev20Destination{Dev20Stage::Station}, Dev20Destination{Dev20Stage::Station, Dev20Entry::Recovery},
        Dev20Destination{Dev20Stage::Button}, Dev20Destination{Dev20Stage::Weight},
        LabDestination::Movement, LabDestination::Optics, LabDestination::Gravity, LabDestination::Portals
    };
    for (const auto& Destination : Routes)
    {
        const auto Decoded = DecodeWorldOptions(EncodeWorldOptions(Destination), WorldFor(Destination));
        CHECK(Decoded.Destination == Destination && !Decoded.InvalidRecoveryOrigin);
    }
    const auto Origin = DecodeWorldOptions("?Checkpoint=Sandbox?Chamber=Passage", EntryWorld::Vanguard);
    CHECK(Origin.InvalidRecoveryOrigin && Origin.Destination == WorldDestination{VanguardDestination{}});
    const auto Legacy = DecodeWorldOptions("?DEV20Run?Dev20Recovered?Name=Player", EntryWorld::Portal);
    CHECK((Legacy.Destination == WorldDestination{Dev20Destination{Dev20Stage::Station, Dev20Entry::Recovery}}));
    CHECK(!DecodeWorldOptions("?Chamber=Unknown", EntryWorld::Chamber).Destination);
    CHECK(!DecodeWorldOptions("?Chamber=Button?chamber=Weight", EntryWorld::Chamber).Destination);
    CHECK(!DecodeWorldOptions("?game=UnknownMode", EntryWorld::Chamber).Destination);
    CHECK(!DecodeWorldOptions("?Chamber=Vanguard?RadiationRun=1", EntryWorld::Vanguard).Destination);
    CHECK(!DecodeWorldOptions("?Chamber=Passage?RadiationRun=1?EntryCourse=1", EntryWorld::Vanguard).Destination);
    CHECK(!DecodeWorldOptions("?Chamber=Sandbox?Dev20Run=1", EntryWorld::Chamber).Destination);
    CHECK(!DecodeWorldOptions("?Dev20Recovered=1", EntryWorld::Portal).Destination);
    CHECK(!DecodeWorldOptions("?EntryCourse=maybe", EntryWorld::Chamber).Destination);
}

void TestCircuitAndBallast()
{
    ChamberCircuit Circuit;
    const CircuitRules Weight{20, 20, CircuitRules::Gate::ButtonAndPlate};
    Circuit.Observe(Weight, 20, false, 0.06f);
    CHECK(!Circuit.GetState().PlatePressed);
    Circuit.Observe(Weight, 20, false, 0.06f);
    CHECK(Circuit.GetState().PlatePressed && !Circuit.GetState().ExitEnabled);
    Circuit.PressButton();
    Circuit.Observe(Weight, 15, false, 0.2f);
    CHECK(!Circuit.GetState().ExitEnabled);
    Circuit.Observe(Weight, 20, false, 0.2f);
    CHECK(Circuit.GetState().ExitEnabled);
    Circuit.Observe(Weight, 25, false, 0.2f);
    CHECK(!Circuit.GetState().ExitEnabled && Circuit.GetState().ButtonLatched);
    Circuit.Observe(Weight, std::numeric_limits<float>::quiet_NaN(), false, 0.2f);
    CHECK(!Circuit.GetState().PlatePressed);

    Circuit.Reset();
    const CircuitRules Stairs{5, 99, CircuitRules::Gate::StackLatch};
    Circuit.Observe(Stairs, 0, true, 0.0f);
    Circuit.Observe(Stairs, 0, false, 0.0f);
    CHECK(Circuit.GetState().ExitEnabled);
    Circuit.Reset();
    CHECK(!Circuit.GetState().ExitEnabled && !Circuit.GetState().ButtonLatched);

    static_assert(AssessStack(27.5, 40, false, true, 1) == StackBlock::Size);
    static_assert(AssessStack(40, 27.5, true, true, 1) == StackBlock::Occupied);
    static_assert(AssessStack(40, 27.5, false, false, 1) == StackBlock::Unstable);
    static_assert(AssessStack(40, 27.5, false, true, 2) == StackBlock::None);
    static_assert(AssessStack(40, 27.5, false, true, 3) == StackBlock::Height);
    static_assert(CarrySpeedScale(5) > CarrySpeedScale(10));
}

void TestSupply()
{
    SuitSupply Supply;
    CHECK(!Supply.SpendInPassage(25));
    CHECK(Supply.Percent() < 100);
    Supply.Refill();
    CHECK(Supply.Percent() == 100);
    CHECK(Supply.SpendInPassage(600));
    CHECK(Supply.Percent() == 0);
}

void TestParallelPlateCircuit()
{
    const CircuitRules Rules{5, 20, CircuitRules::Gate::ButtonAndParallelPlates};
    ChamberCircuit Circuit;
    Circuit.Observe(Rules, 5, false, 0.06f);
    Circuit.Observe(Rules, 5, false, 0.06f, 5);
    CHECK(Circuit.GetState().PlatePressed && !Circuit.GetState().AlternatePlatePressed);
    CHECK(Circuit.GetState().ParallelOutput && !Circuit.GetState().ExitEnabled);
    Circuit.PressButton();
    Circuit.Observe(Rules, 5, false, 0.0f, 5);
    CHECK(Circuit.GetState().ExitEnabled);
    Circuit.Observe(Rules, 10, false, 0.06f, 5);
    CHECK(Circuit.GetState().PlatePressed && Circuit.GetState().AlternatePlatePressed);
    Circuit.Observe(Rules, 85, false, 0.0f, 20);
    CHECK(!Circuit.GetState().PlatePressed && Circuit.GetState().AlternatePlatePressed);
    CHECK(Circuit.GetState().ExitEnabled);
    Circuit.Observe(Rules, 4, false, 1.0f, 21);
    CHECK(!Circuit.GetState().ParallelOutput && !Circuit.GetState().ExitEnabled);
    Circuit.Observe(Rules, 0, false, 0.2f, 5);
    CHECK(Circuit.GetState().ExitEnabled);
    Circuit.Reset();
    Circuit.Observe(Rules, 0, false, 0.06f, 5);
    CHECK(!Circuit.GetState().ButtonLatched && !Circuit.GetState().ParallelOutput);
}

void TestFallImpact()
{
    CHECK(AssessFall(0.0) == FallImpact::None);
    CHECK(AssessFall(299.9) == FallImpact::None);
    CHECK(AssessFall(300.0) == FallImpact::MinorInjury);
    CHECK(AssessFall(300.1) == FallImpact::Fatal);
    CHECK(AssessFall(std::numeric_limits<double>::quiet_NaN()) == FallImpact::None);

    constexpr InjuryRecoveryProfile Profile{2.5, 0.65f};
    MinorInjuryRecovery Recovery;
    CHECK(!Recovery.IsActive() && Recovery.GroundSpeedScale() == 1.0f);
    Recovery.Start({0.0, 0.65f});
    CHECK(!Recovery.IsActive());
    Recovery.Start(Profile);
    Recovery.Advance(-1.0);
    Recovery.Advance(std::numeric_limits<double>::quiet_NaN());
    CHECK(Recovery.IsActive() && Recovery.GroundSpeedScale() == Profile.InitialSpeedScale);
    Recovery.Advance(1.25);
    CHECK(std::abs(Recovery.GroundSpeedScale() - 0.825f) < 0.0001f);
    Recovery.Advance(1.25);
    CHECK(!Recovery.IsActive() && Recovery.GroundSpeedScale() == 1.0f);
    Recovery.Start(Profile);
    Recovery.Advance(2.0);
    Recovery.Start(Profile);
    CHECK(Recovery.GroundSpeedScale() == Profile.InitialSpeedScale);
    Recovery.Reset();
    CHECK(!Recovery.IsActive() && Recovery.GroundSpeedScale() == 1.0f);
}

void TestPassageHazards()
{
    PassageHazards Passage;
    SuitSession Suit;
    constexpr InjuryRecoveryProfile Profile{2.5, 0.65f};
    CHECK(!Passage.Land(0.0));
    CHECK(!Passage.AdvanceMovement(300.02, 0.0f, Profile));
    CHECK(Passage.Land(0.0) == 300.0);
    CHECK(!Passage.Land(0.0));
    CHECK(Passage.AdvanceMovement(0.0, 0.1f, Profile));
    CHECK(!Passage.AdvanceMovement(0.0, 2.5f, Profile));
    CHECK(!Passage.Recovery().IsActive());

    Passage.AdvanceMovement(300.1, 0.0f, Profile);
    Passage.Land(0.0);
    const auto FatalAtExit = Passage.Observe(true, false, RadiationPosition::Outside, 0.1f, &Suit);
    CHECK(FatalAtExit.Impact == FallImpact::Fatal);
    CHECK(ResolvePassage(FatalAtExit) == PassageOutcome::ReturnToCheckpoint);
    Passage.ResetMovement();
    CHECK(!Passage.SupportHeight());
    CHECK(ResolvePassage(Passage.Observe(true, false, RadiationPosition::Outside, 0.0f, &Suit)) == PassageOutcome::EnterSanctuary);

    Passage.EnableRadiation({1.0, 1.0, 1.0, 10.0, 1.0});
    const auto Simultaneous = Passage.Observe(true, false, RadiationPosition::Exposed, 600.0f, &Suit);
    CHECK(Simultaneous.RadiationLimitReached && Simultaneous.OxygenDepleted);
    CHECK(ResolvePassage(Simultaneous) == PassageOutcome::ReturnToCheckpoint);
}

void TestMantleRules()
{
    static_assert(CanReachMantleLedge(140.0, 85.0, 1.0));
    static_assert(!CanReachMantleLedge(90.0, 52.0, 1.0));
    static_assert(CanReachMantleLedge(180.0, 130.0, 1.0));
    static_assert(!CanReachMantleLedge(190.0, 85.0, 1.0));
    static_assert(!CanReachMantleLedge(140.0, 18.0, 1.0));
    static_assert(!CanReachMantleLedge(140.0, 85.0, 0.60));

    MantleRequest Gesture;
    Gesture.Press();
    Gesture.Advance(0.08);
    CHECK(!Gesture.IsReady());
    Gesture.Release();
    Gesture.Advance(1.0);
    CHECK(!Gesture.IsReady());
    Gesture.Press();
    Gesture.Advance(std::numeric_limits<double>::quiet_NaN());
    CHECK(!Gesture.IsReady());
    Gesture.Advance(MantleHoldSeconds);
    CHECK(Gesture.IsReady());
    Gesture.Consume();
    Gesture.Press();
    Gesture.Advance(1.0);
    CHECK(!Gesture.IsReady());
    Gesture.Release();
    Gesture.Press();
    Gesture.Advance(MantleHoldSeconds);
    CHECK(Gesture.IsReady());
}

void TestRadiationExposure()
{
    using Position = RadiationPosition;
    const FlarePattern Pattern{4.0, 2.0, 2.0, 30.0, 3.0};
    CHECK(Pattern.IsValid());
    const FlarePattern Invalid{0.0, 2.0, 2.0, 30.0, 3.0};
    CHECK(!Invalid.IsValid());
    const FlarePattern NoRecovery{4.0, 2.0, 2.0, 30.0, 0.0};
    CHECK(!NoRecovery.IsValid());
    RadiationDose Dose;
    RadiationExposure Radiation{Pattern};
    CHECK(Radiation.Read(Position::Outside, Dose).SecondsToChange == 4.0);
    CHECK(!Radiation.Advance(3.0, Position::Outside, Dose));
    CHECK(Radiation.Read(Position::Outside, Dose).DosePercent == 0.0);
    CHECK(!Radiation.Advance(1.5, Position::Exposed, Dose));
    const auto Flare = Radiation.Read(Position::Exposed, Dose);
    CHECK(Flare.FlareActive && Flare.DosePercent == 17.0 && Flare.DosePerSecond == 30.0 && Flare.SecondsToChange == 1.5);
    CHECK(!Radiation.Advance(2.0, Position::Sheltered, Dose));
    const auto Shelter = Radiation.Read(Position::Sheltered, Dose);
    CHECK(!Shelter.FlareActive && Shelter.DosePercent == 17.0 && Shelter.DosePerSecond == 0.0);
    CHECK(Shelter.SecondsToChange == 3.5);
    CHECK(!Radiation.Advance(0.5, Position::Outside, Dose));
    CHECK(Dose.Percent() == 15.5 && Radiation.Read(Position::Outside, Dose).DosePerSecond == -3.0);
    CHECK(!Radiation.Advance(6.0, Position::Outside, Dose));
    CHECK(Dose.Percent() == 0.0 && Radiation.Read(Position::Outside, Dose).DosePerSecond == 0.0);
    CHECK(!Radiation.Advance(std::numeric_limits<double>::quiet_NaN(), Position::Exposed, Dose));
    CHECK(Radiation.Advance(18.0, Position::Exposed, Dose));
    CHECK(Dose.Percent() == RadiationDose::Limit && Dose.IsLethal());
    CHECK(Radiation.Advance(60.0, Position::Outside, Dose) && Dose.IsLethal());
    Dose.Clear();
    CHECK(Dose.Percent() == 0.0);

    RadiationExposure Stepwise{Pattern};
    RadiationExposure Batched{Pattern};
    RadiationDose StepwiseDose;
    RadiationDose BatchedDose;
    for (int Index = 0; Index < 112; ++Index)
    {
        CHECK(!Stepwise.Advance(0.0625, Position::Exposed, StepwiseDose));
    }
    CHECK(!Batched.Advance(7.0, Position::Exposed, BatchedDose));
    CHECK(StepwiseDose.Percent() == BatchedDose.Percent());

    static_assert(ResolvePassage({}) == PassageOutcome::Continue);
    static_assert(ResolvePassage({.ReachedExit = true}) == PassageOutcome::EnterSanctuary);
    static_assert(ResolvePassage({.ReachedExit = true, .RadiationLimitReached = true}) == PassageOutcome::ReturnToCheckpoint);
    static_assert(ResolvePassage({.Impact = FallImpact::Fatal}) == PassageOutcome::ReturnToCheckpoint);
}

void TestSlidingDoor()
{
    static_assert(ResolveClosingContact(true, DoorContact::Carryable, true) == ClosingContact::Clamp);
    static_assert(ResolveClosingContact(false, DoorContact::Carryable, true) == ClosingContact::Block);
    static_assert(ResolveClosingContact(true, DoorContact::None, true) == ClosingContact::Block);
    static_assert(ResolveClosingContact(true, DoorContact::Player, true) == ClosingContact::ResetPlayer);
    static_assert(ResolveClosingContact(true, DoorContact::Player, false) == ClosingContact::Block);
    static_assert(!CanReleaseDoorClamp(false, 60.0, 55.0));
    static_assert(!CanReleaseDoorClamp(true, 58.0, 55.0));
    static_assert(CanReleaseDoorClamp(true, 59.0, 55.0));
}

void TestChamberHints()
{
    using Abandoned::Application::Puzzles::ChamberHintReader;
    constexpr double Delay = 360.0;
    ChamberHintReader Reader;
    CHECK(Reader.Page() == 0 && Reader.AvailablePages() == 1 && !Reader.HasNewEntry());
    Reader.Activate();
    CHECK(Reader.Page() == 1);
    Reader.Activate();
    CHECK(Reader.Page() == 0);
    Reader.Observe(0, false, 300.0, Delay);
    Reader.Observe(ArmedButtonHintNode, false, 1.0, Delay);
    Reader.Observe(ArmedButtonHintNode, false, 200.0, Delay);
    Reader.Observe(0, false, 159.0, Delay);
    CHECK(Reader.AvailablePages() == 1);
    Reader.Observe(ArmedButtonHintNode, false, 1.0, Delay);
    CHECK(Reader.AvailablePages() == 2 && Reader.HasNewEntry() && Reader.Page() == 0);
    Reader.Activate();
    CHECK(Reader.HasNewEntry());
    Reader.Activate();
    CHECK(Reader.Page() == 2 && !Reader.HasNewEntry());
    Reader.Close();
    Reader.Observe(PressurePlateHintNode(0), false, 1.0, Delay);
    Reader.Observe(0, true, 360.0, Delay);
    Reader.Observe(0, false, std::numeric_limits<double>::quiet_NaN(), Delay);
    Reader.Observe(0, false, 359.0, Delay);
    CHECK(Reader.AvailablePages() == 2);
    Reader.Observe(0, false, 1.0, Delay);
    CHECK(Reader.AvailablePages() == 3 && Reader.HasNewEntry());
    Reader = {};
    CHECK(Reader.AvailablePages() == 1 && !Reader.HasNewEntry());
    static_assert(PressurePlateHintNode(0) != PressurePlateHintNode(1));
    static_assert(PressurePlateHintNode(-1) == 0 && PressurePlateHintNode(MaximumHintPlates) == 0);
}

void TestPowerSignalsAndOptics()
{
    const bool Mixed[] = {false, true};
    const bool Live[] = {true, true};
    CHECK(AnyPowered(Mixed) && !AllPowered(Mixed) && AllPowered(Live));
    CHECK(!AnyPowered({}) && !AllPowered({}) && !InvertPower(true));

    SignalOutput Toggle{SignalMode::Toggle};
    CHECK(Toggle.Observe(true, 0.0f));
    CHECK(Toggle.Observe(true, 10.0f));
    CHECK(Toggle.Observe(false, 0.0f));
    CHECK(!Toggle.Observe(true, 0.0f));

    SignalOutput Pulse{SignalMode::Pulse, 0.25f};
    CHECK(Pulse.Observe(true, 0.1f));
    CHECK(Pulse.Observe(true, 0.2f));
    CHECK(!Pulse.Observe(true, 0.1f));
    CHECK(!Pulse.Observe(false, 0.0f));
    CHECK(Pulse.Observe(true, 0.0f));
    Pulse.Reset();
    CHECK(!Pulse.IsPowered());

    PowerBudget Budget{3};
    CHECK(!Budget.PowerBranch(false, 2));
    CHECK(!Budget.PowerBranch(true, 4));
    CHECK(Budget.PowerBranch(true, 2));
    CHECK(!Budget.PowerBranch(true, 2));
    CHECK(Budget.PowerBranch(true, 1));

    Abandoned::Application::Puzzles::OpticsCourse Optics;
    CHECK(!Optics.ObserveReceivers(false, true, 1.0f).Interruption);
    Optics.PressSupplyButton();
    CHECK(Optics.GetPower().DirectEmitter && !Optics.GetPower().ReflectionEmitter);
    CHECK(!Optics.ObserveReceivers(false, false, 0.15f).Interruption);
    CHECK(!Optics.ObserveReceivers(true, false, 0.15f).Interruption);
    CHECK(!Optics.ObserveReceivers(false, false, 0.15f).Interruption);
    CHECK(Optics.ObserveReceivers(false, false, 0.2f).Interruption);
    CHECK(Optics.GetPower().ReflectionEmitter);
    CHECK(Optics.ObserveReceivers(true, true, 0.3f).Reflection);
    CHECK(!Optics.ObserveReceivers(true, false, 0.0f).Reflection);
    Optics.Reset();
    CHECK(!Optics.GetPower().DirectEmitter && !Optics.GetPower().ReflectionEmitter);
}

void TestOpticalReflector()
{
    CHECK(IsReflectorSettled(1.0, 25.0, 36.0));
    CHECK(!IsReflectorSettled(0.2, 0.0, 0.0));
    CHECK(!IsReflectorSettled(1.0, 400.0, 0.0));
    CHECK(!IsReflectorSettled(1.0, 0.0, 400.0));
    CHECK(!IsReflectorSettled(1.0, std::numeric_limits<double>::quiet_NaN(), 0.0));
    static_assert(CanReflectFromFace(1.0, -0.5));
    static_assert(!CanReflectFromFace(-1.0, -0.5));
    static_assert(!CanReflectFromFace(0.0, -0.5));
    static_assert(!CanReflectFromFace(1.0, 0.0));
}

void TestGravityFields()
{
    const GravityField Contact[] = {
        {GravityDirection::Up, 1.0f, 9000.0f, 3, false},
        {GravityDirection::Down, 1.0f, 9000.0f, 0, true},
        {GravityDirection::Left, 0.8f, 9000.0f, 2, true},
        {GravityDirection::Right, 4.0f, 9000.0f, 2, true},
    };
    const GravityField* Field = SelectGravityField(Contact);
    CHECK(Field == &Contact[2]);
    CHECK(!SelectGravityField({}));
    CHECK(!SelectGravityField(std::span(Contact, 1)));
    CHECK(GravityForceForBody(*Field, 5.0f) / 5.0f == 2.0f * GravityForceForBody(*Field, 10.0f) / 10.0f);
    CHECK(GravityForceForBody(*Field, 0.0f) == 0.0f);
    CHECK(GravityRollDegrees(GravityDirection::Left) == -90.0f);
    CHECK(GravityRollDegrees(GravityDirection::Right) == 90.0f);
    CHECK(GravityRollDegrees(GravityDirection::Up) == 180.0f);
}

void TestTimedPassage()
{
    Abandoned::Application::Puzzles::TimedPassage Passage{3.5f};
    CHECK(!Passage.IsPowered() && !Passage.IsComplete() && Passage.RemainingSeconds() == 0.0f);
    Passage.Press();
    Passage.Observe(0.5f, false);
    Passage.Press();
    Passage.Observe(std::numeric_limits<float>::quiet_NaN(), false);
    CHECK(Passage.IsPowered() && Passage.RemainingSeconds() == 3.0f);
    Passage.Observe(3.0f, false);
    CHECK(!Passage.IsPowered() && !Passage.IsComplete());
    Passage.Press();
    CHECK(Passage.RemainingSeconds() == 3.5f);
    Passage.Observe(0.25f, true);
    Passage.Observe(3.5f, false);
    CHECK(!Passage.IsPowered() && Passage.IsComplete());
    Passage.Press();
    CHECK(Passage.IsPowered() && Passage.IsComplete());
    Passage.Reset();
    CHECK(!Passage.IsPowered() && !Passage.IsComplete() && Passage.RemainingSeconds() == 0.0f);
}

void TestPortalTransit()
{
    CHECK(FitsPortal(0, -53, 118, 145, 30, 90));
    CHECK(!FitsPortal(96, 0, 118, 145, 30, 90));
    CHECK(!FitsPortal(0, 75, 118, 145, 30, 90));
    static_assert(ObservePortal(false, false, true, false, true, false) == PortalEvent::None);
    static_assert(ObservePortal(true, false, true, false, true, false) == PortalEvent::Approach);
    static_assert(ObservePortal(true, true, false, true, true, true) == PortalEvent::Transfer);
    static_assert(ObservePortal(true, false, false, true, true, true) == PortalEvent::Transfer);
    static_assert(ObservePortal(true, true, false, true, false, true) == PortalEvent::Blocked);
    static_assert(ObservePortal(true, true, false, true, true, false) == PortalEvent::Blocked);
    static_assert(ObservePortal(false, true, true, false, true, false) == PortalEvent::Interrupted);
    static_assert(ObservePortal(false, true, false, true, true, false) == PortalEvent::Interrupted);
    static_assert(ObservePortal(false, true, false, false, true, false) == PortalEvent::None);
}

void TestDev20Transfer()
{
    using Stage = Dev20Stage;
    static_assert(Dev20Id == "DEV-20");
    static_assert(Dev20MassKg == 34.0f && Dev20MassKg <= MaximumCarryKg);
    static_assert(CarrySpeedScale(Dev20MassKg) < CarrySpeedScale(10.0f));
    Dev20Transfer Transfer;
    CHECK(!Transfer.Arrive(Stage::Button));
    CHECK(!Transfer.Depart(Stage::Station, false));
    CHECK(!Transfer.Depart(Stage::Button, true));
    CHECK(Transfer.Depart(Stage::Station, true) == Stage::Button);
    CHECK(!Transfer.Depart(Stage::Station, true));
    CHECK(!Transfer.Arrive(Stage::Weight));
    CHECK(Transfer.Arrive(Stage::Button));
    CHECK(!Transfer.Arrive(Stage::Button));
    CHECK(Transfer.Depart(Stage::Button, true) == Stage::Weight);
    Transfer.RecoverAtStation();
    CHECK(!Transfer.Arrive(Stage::Weight));
    CHECK(Transfer.Depart(Stage::Station, true) == Stage::Button);
    CHECK(Transfer.Arrive(Stage::Button));
    CHECK(Transfer.Depart(Stage::Button, true) == Stage::Weight);
    CHECK(Transfer.Arrive(Stage::Weight));
    CHECK(!Transfer.Depart(Stage::Weight, true));
    CHECK(!Transfer.AbandonHeldUnit(false));
    CHECK(Transfer.AbandonHeldUnit(true));
    CHECK(!Transfer.AbandonHeldUnit(true));
    CHECK(Transfer.EvacuationLost());
    Transfer.RecoverAtStation();
    CHECK(!Transfer.EvacuationLost());
    CHECK(Transfer.AbandonHeldUnit(true));
    CHECK(!Transfer.Depart(Stage::Station, true));
    Transfer.RecoverAtStation();
    CHECK(Transfer.Depart(Stage::Station, true) == Stage::Button);
    CHECK(!Transfer.AbandonHeldUnit(true));
}

void TestPlateBalance()
{
    constexpr PlateBalanceRules Rules{30.0f, 0.01f};
    constexpr float Underload[] = {5, 5, 5};
    constexpr float Uneven[] = {20, 10, 0};
    constexpr float Target[] = {10, 10, 10};
    constexpr float PlayerLoad[] = {10, 100, 10};
    const float Invalid[] = {10, std::numeric_limits<float>::quiet_NaN(), 10};
    PlateBalance Balance;
    Balance.Observe(Rules, {}, 1.0f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Underload, 1.0f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Uneven, 1.0f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.15f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.10f);
    CHECK(Balance.IsBalanced());
    Balance.Observe(Rules, PlayerLoad, 0.0f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.15f);
    Balance.Observe(Rules, Invalid, 0.0f);
    Balance.Observe(Rules, Target, 0.15f);
    CHECK(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 1.0f);
    CHECK(Balance.IsBalanced());
    Balance.Reset();
    CHECK(!Balance.IsBalanced());
    static_assert(ParseSegment("Balance") == Segment::Balance);
}

void TestSuitTerminal()
{
    SuitArchive Archive;
    SuitTerminal Terminal;
    CHECK(!Archive.At(0));
    Terminal.NextPage();
    CHECK(!Terminal.Page());
    Terminal.Toggle();
    CHECK(Terminal.Page() == SuitPage::Status);
    Terminal.NextPage();
    CHECK(Terminal.Page() == SuitPage::Signal);
    Terminal.NextPage();
    CHECK(Terminal.Page() == SuitPage::Archive);
    Terminal.MoveArchive(true, Archive);
    CHECK(Terminal.ArchiveIndex() == 0);
    Archive.Reveal(SuitRecord::ForecastSynchronized);
    Archive.Reveal(SuitRecord::Decontaminated);
    Archive.Reveal(SuitRecord::ForecastSynchronized);
    CHECK(Archive.Size() == 2);
    Terminal.MoveArchive(true, Archive);
    Terminal.MoveArchive(true, Archive);
    CHECK(Archive.At(Terminal.ArchiveIndex()) == SuitRecord::Decontaminated);
    Terminal.MoveArchive(false, Archive);
    Terminal.MoveArchive(false, Archive);
    CHECK(Archive.At(Terminal.ArchiveIndex()) == SuitRecord::ForecastSynchronized);
    Terminal.NextPage();
    CHECK(Terminal.Page() == SuitPage::Status);
    Terminal.MoveArchive(true, Archive);
    CHECK(Terminal.ArchiveIndex() == 0);
    Terminal.Close();
    CHECK(!Terminal.Page());
    Terminal.OpenArchive();
    CHECK(Terminal.Page() == SuitPage::Archive && Terminal.ArchiveIndex() == 0);
    SuitSession Session;
    Session.RestoreRecords(Archive);
    CHECK(!Session.WasRead(SuitRecord::ForecastSynchronized));
    Session.MarkRead(SuitRecord::ForecastSynchronized);
    CHECK(Session.WasRead(SuitRecord::ForecastSynchronized));
    CHECK(!Session.WasRead(SuitRecord::Decontaminated) && Session.Records().Size() == 2);

    RadiationExposure Exposure({4.5, 3.0, 2.0, 35.0, 3.0});
    RadiationDose Dose;
    const SuitTelemetry Approach{99.0f, 0.0, Exposure.Read(RadiationPosition::Outside, Dose), false};
    CHECK(Approach.Signal() == SuitSignal::ExteriorForecast && Approach.Forecast->SecondsToChange == 4.5);
    const SuitTelemetry Passage{99.0f, 0.0, std::nullopt, false};
    CHECK(Passage.Signal() == SuitSignal::Unavailable);
    const SuitTelemetry Station{98.0f, 0.0, std::nullopt, true};
    CHECK(Station.Signal() == SuitSignal::VanguardStation && !Station.Forecast);
}

void TestSuitSessionRecovery()
{
    SuitSession Suit;
    CHECK(Suit.EnterStation() == 0.0);
    CHECK(Suit.Records().Size() == 0);
    Suit.EnterPassage(false);
    CHECK(Suit.Records().Size() == 0);

    constexpr FlarePattern Schedule{4.5, 3.0, 2.0, 35.0, 3.0};
    RadiationExposure FirstRun(Schedule);
    Suit.EnterPassage(true);
    Suit.SetFlashlight(true);
    CHECK(!Suit.SpendInPassage(40.0f));
    CHECK(Suit.AdvanceExposure(FirstRun, RadiationPosition::Exposed, 20.0));
    CHECK(Suit.RadiationDose().IsLethal());

    // A fresh passage world starts a fresh forecast cycle while retaining the session archive.
    Suit.EnterPassage(true);
    CHECK(Suit.OxygenPercent() == 100.0f);
    CHECK(Suit.RadiationDose().Percent() == 0.0);
    CHECK(!Suit.IsFlashlightOn());
    CHECK(Suit.Records().Size() == 1);
    CHECK(Suit.Records().At(0) == SuitRecord::ForecastSynchronized);

    RadiationExposure Retry(Schedule);
    CHECK(!Suit.AdvanceExposure(Retry, RadiationPosition::Exposed, 2.0));
    CHECK(!Suit.SpendInPassage(25.0f));
    Suit.SetFlashlight(true);
    const float RemainingOxygen = Suit.OxygenPercent();
    CHECK(RemainingOxygen < 100.0f);
    CHECK(Suit.EnterStation() == 4.0);
    CHECK(Suit.EnterStation() == 0.0);
    CHECK(Suit.RadiationDose().Percent() == 0.0);
    CHECK(Suit.OxygenPercent() == RemainingOxygen && Suit.IsFlashlightOn());
    CHECK(Suit.Records().At(1) == SuitRecord::Decontaminated);

    Suit.Resupply();
    Suit.Resupply();
    CHECK(Suit.OxygenPercent() == 100.0f && Suit.IsFlashlightOn());
    CHECK(Suit.Records().Size() == 3);
    CHECK(Suit.Records().At(2) == SuitRecord::OxygenRestored);
    CHECK(SuitSession{}.Records().Size() == 0);
}
}

int main(int Count, char** Arguments)
{
    if (Count == 2 && std::string_view(Arguments[1]) == "--verify-failure-path")
    {
        TestChecks::CurrentTest = "FailurePath";
        CHECK(false);
    }
    struct TestCase
    {
        const char* Name;
        void (*Run)();
    };
    const TestCase Tests[] = {
        {"TestRouteAndCheckpoint", TestRouteAndCheckpoint},
        {"TestPassageRecovery", TestPassageRecovery},
        {"TestWorldRouteOptions", TestWorldRouteOptions},
        {"TestCircuitAndBallast", TestCircuitAndBallast},
        {"TestParallelPlateCircuit", TestParallelPlateCircuit},
        {"TestSupply", TestSupply},
        {"TestFallImpact", TestFallImpact},
        {"TestPassageHazards", TestPassageHazards},
        {"TestMantleRules", TestMantleRules},
        {"TestRadiationExposure", TestRadiationExposure},
        {"TestChamberHints", TestChamberHints},
        {"TestSlidingDoor", TestSlidingDoor},
        {"TestPowerSignalsAndOptics", TestPowerSignalsAndOptics},
        {"TestOpticalReflector", TestOpticalReflector},
        {"TestTimedPassage", TestTimedPassage},
        {"TestGravityFields", TestGravityFields},
        {"TestPortalTransit", TestPortalTransit},
        {"TestDev20Transfer", TestDev20Transfer},
        {"TestPlateBalance", TestPlateBalance},
        {"TestSuitTerminal", TestSuitTerminal},
        {"TestSuitSessionRecovery", TestSuitSessionRecovery},
    };
    for (const TestCase& Test : Tests)
    {
        TestChecks::CurrentTest = Test.Name;
        Test.Run();
        std::cout << Test.Name << " passed\n";
    }
}
