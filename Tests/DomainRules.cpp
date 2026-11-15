#include "Application/Expedition/EntryCourse.h"
#include "Application/Expedition/Dev20Transfer.h"
#include "Application/Expedition/PassageProgress.h"
#include "Application/Puzzles/OpticsCourse.h"
#include "Domain/Expedition/FallImpact.h"
#include "Domain/Expedition/RadiationExposure.h"
#include "Domain/Expedition/SuitSupply.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Domain/Puzzles/ChamberCircuit.h"
#include "Domain/Puzzles/GravityField.h"
#include "Domain/Puzzles/PortalTransit.h"
#include "Domain/Puzzles/PlateBalance.h"

#include <cassert>
#include <limits>

using namespace Abandoned::Domain::Expedition;
using namespace Abandoned::Domain::Puzzles;
using namespace Abandoned::Application::Expedition;

namespace
{
struct MemoryCheckpoint final : CheckpointStore
{
    mutable Checkpoint Data;
    mutable int Writes = 0;

    Checkpoint Read() const override { return Data; }
    bool Write(Segment Value) const override
    {
        ++Writes;
        Data = {Value, false};
        return true;
    }
};

void TestRouteAndCheckpoint()
{
    static_assert(NextSegment(Segment::Stairs) == Segment::Button);
    static_assert(NextSegment(Segment::Button) == Segment::Weight);
    static_assert(NextSegment(Segment::Weight) == Segment::Passage);
    static_assert(NextSegment(Segment::Passage) == Segment::Vanguard);
    static_assert(!NextSegment(Segment::Vanguard));
    static_assert(!NextSegment(Segment::Sandbox));
    static_assert(!ParseSegment("Unknown"));
    static_assert(ParseSegment(SegmentKey(Segment::Weight)) == Segment::Weight);

    MemoryCheckpoint Store;
    assert(StartCourse(std::nullopt, &Store).Current == Segment::Stairs);
    assert(RecordArrival({Segment::Weight}, Store));
    assert(RecordArrival({Segment::Passage}, Store));
    assert(RecordArrival({Segment::Vanguard}, Store));
    assert(Store.Writes == 1);
    assert(StartCourse(std::nullopt, &Store).Current == Segment::Weight);
    assert(StartCourse(Segment::Button, &Store).Current == Segment::Button);

    Store.Data = {Segment::Vanguard, false};
    const CourseStart Recovered = StartCourse(std::nullopt, &Store);
    assert(Recovered.CheckpointFallback && Recovered.Current == Segment::Stairs);
    assert(RecordArrival(Recovered, Store));
    assert(Store.Writes == 1);
}

void TestCircuitAndBallast()
{
    ChamberCircuit Circuit;
    const CircuitRules Weight{20, 20, CircuitRules::Gate::ButtonAndPlate};
    Circuit.Observe(Weight, 20, false, 0.06f);
    assert(!Circuit.GetState().PlatePressed);
    Circuit.Observe(Weight, 20, false, 0.06f);
    assert(Circuit.GetState().PlatePressed && !Circuit.GetState().ExitEnabled);
    Circuit.PressButton();
    Circuit.Observe(Weight, 15, false, 0.2f);
    assert(!Circuit.GetState().ExitEnabled);
    Circuit.Observe(Weight, 20, false, 0.2f);
    assert(Circuit.GetState().ExitEnabled);
    Circuit.Observe(Weight, 25, false, 0.2f);
    assert(!Circuit.GetState().ExitEnabled && Circuit.GetState().ButtonLatched);
    Circuit.Observe(Weight, std::numeric_limits<float>::quiet_NaN(), false, 0.2f);
    assert(!Circuit.GetState().PlatePressed);

    Circuit.Reset();
    const CircuitRules Stairs{5, 99, CircuitRules::Gate::StackLatch};
    Circuit.Observe(Stairs, 0, true, 0.0f);
    Circuit.Observe(Stairs, 0, false, 0.0f);
    assert(Circuit.GetState().ExitEnabled);
    Circuit.Reset();
    assert(!Circuit.GetState().ExitEnabled && !Circuit.GetState().ButtonLatched);

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
    assert(!Supply.SpendInPassage(25));
    assert(Supply.Percent() < 100);
    Supply.Refill();
    assert(Supply.Percent() == 100);
    assert(Supply.SpendInPassage(600));
    assert(Supply.Percent() == 0);
}

void TestFallImpact()
{
    assert(AssessFall(0.0) == FallImpact::None);
    assert(AssessFall(299.9) == FallImpact::None);
    assert(AssessFall(300.0) == FallImpact::MinorInjury);
    assert(AssessFall(300.1) == FallImpact::Fatal);
    assert(AssessFall(std::numeric_limits<double>::quiet_NaN()) == FallImpact::None);
}

void TestRadiationExposure()
{
    using Position = RadiationPosition;
    const FlarePattern Pattern{4.0, 2.0, 2.0, 30.0};
    assert(Pattern.IsValid());
    const FlarePattern Invalid{0.0, 2.0, 2.0, 30.0};
    assert(!Invalid.IsValid());
    RadiationExposure Radiation{Pattern};
    assert(Radiation.Read(Position::Outside).SecondsToChange == 4.0);
    assert(!Radiation.Advance(3.0, Position::Outside));
    assert(Radiation.Read(Position::Outside).DosePercent == 0.0);
    assert(!Radiation.Advance(1.5, Position::Exposed));
    const auto Flare = Radiation.Read(Position::Exposed);
    assert(Flare.FlareActive && Flare.DosePercent == 17.0 && Flare.DosePerSecond == 30.0 && Flare.SecondsToChange == 1.5);
    assert(!Radiation.Advance(2.0, Position::Sheltered));
    const auto Shelter = Radiation.Read(Position::Sheltered);
    assert(!Shelter.FlareActive && Shelter.DosePercent == 17.0 && Shelter.DosePerSecond == 0.0);
    assert(Shelter.SecondsToChange == 3.5);
    assert(!Radiation.Advance(std::numeric_limits<double>::quiet_NaN(), Position::Exposed));
    assert(Radiation.Advance(12.0, Position::Exposed));
    assert(Radiation.Read(Position::Exposed).DosePercent == RadiationExposure::DoseLimit);

    RadiationExposure Stepwise{Pattern};
    RadiationExposure Batched{Pattern};
    for (int Index = 0; Index < 112; ++Index)
    {
        assert(!Stepwise.Advance(0.0625, Position::Exposed));
    }
    assert(!Batched.Advance(7.0, Position::Exposed));
    assert(Stepwise.Read(Position::Exposed).DosePercent == Batched.Read(Position::Exposed).DosePercent);

    static_assert(ResolvePassage({}) == PassageOutcome::Continue);
    static_assert(ResolvePassage({.ReachedExit = true}) == PassageOutcome::EnterSanctuary);
    static_assert(ResolvePassage({.ReachedExit = true, .RadiationLimitReached = true}) == PassageOutcome::ReturnToCheckpoint);
    static_assert(ResolvePassage({.Impact = FallImpact::Fatal}) == PassageOutcome::ReturnToCheckpoint);
}

void TestPowerSignalsAndOptics()
{
    const bool Mixed[] = {false, true};
    const bool Live[] = {true, true};
    assert(AnyPowered(Mixed) && !AllPowered(Mixed) && AllPowered(Live));
    assert(!AnyPowered({}) && !AllPowered({}) && !InvertPower(true));

    SignalOutput Toggle{SignalMode::Toggle};
    assert(Toggle.Observe(true, 0.0f));
    assert(Toggle.Observe(true, 10.0f));
    assert(Toggle.Observe(false, 0.0f));
    assert(!Toggle.Observe(true, 0.0f));

    SignalOutput Pulse{SignalMode::Pulse, 0.25f};
    assert(Pulse.Observe(true, 0.1f));
    assert(Pulse.Observe(true, 0.2f));
    assert(!Pulse.Observe(true, 0.1f));
    assert(!Pulse.Observe(false, 0.0f));
    assert(Pulse.Observe(true, 0.0f));
    Pulse.Reset();
    assert(!Pulse.IsPowered());

    PowerBudget Budget{3};
    assert(!Budget.PowerBranch(false, 2));
    assert(!Budget.PowerBranch(true, 4));
    assert(Budget.PowerBranch(true, 2));
    assert(!Budget.PowerBranch(true, 2));
    assert(Budget.PowerBranch(true, 1));

    Abandoned::Application::Puzzles::OpticsCourse Optics;
    assert(!Optics.ObserveReceivers(false, true, 1.0f).Interruption);
    Optics.PressSupplyButton();
    assert(Optics.GetPower().DirectEmitter && !Optics.GetPower().ReflectionEmitter);
    assert(!Optics.ObserveReceivers(false, false, 0.15f).Interruption);
    assert(!Optics.ObserveReceivers(true, false, 0.15f).Interruption);
    assert(!Optics.ObserveReceivers(false, false, 0.15f).Interruption);
    assert(Optics.ObserveReceivers(false, false, 0.2f).Interruption);
    assert(Optics.GetPower().ReflectionEmitter);
    assert(Optics.ObserveReceivers(true, true, 0.3f).Reflection);
    assert(!Optics.ObserveReceivers(true, false, 0.0f).Reflection);
    Optics.Reset();
    assert(!Optics.GetPower().DirectEmitter && !Optics.GetPower().ReflectionEmitter);
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
    assert(Field == &Contact[2]);
    assert(!SelectGravityField({}));
    assert(!SelectGravityField(std::span(Contact, 1)));
    assert(GravityForceForBody(*Field, 5.0f) / 5.0f == 2.0f * GravityForceForBody(*Field, 10.0f) / 10.0f);
    assert(GravityForceForBody(*Field, 0.0f) == 0.0f);
    assert(GravityRollDegrees(GravityDirection::Left) == -90.0f);
    assert(GravityRollDegrees(GravityDirection::Right) == 90.0f);
    assert(GravityRollDegrees(GravityDirection::Up) == 180.0f);
}

void TestPortalTransit()
{
    assert(FitsPortal(0, -53, 118, 145, 30, 90));
    assert(!FitsPortal(96, 0, 118, 145, 30, 90));
    assert(!FitsPortal(0, 75, 118, 145, 30, 90));
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
    assert(!Transfer.Arrive(Stage::Button));
    assert(!Transfer.Depart(Stage::Station, false));
    assert(!Transfer.Depart(Stage::Button, true));
    assert(Transfer.Depart(Stage::Station, true));
    assert(!Transfer.Depart(Stage::Station, true));
    assert(!Transfer.Arrive(Stage::Weight));
    assert(Transfer.Arrive(Stage::Button));
    assert(!Transfer.Arrive(Stage::Button));
    assert(Transfer.Depart(Stage::Button, true));
    Transfer.RecoverAtStation();
    assert(!Transfer.Arrive(Stage::Weight));
    assert(Transfer.Depart(Stage::Station, true));
    assert(Transfer.Arrive(Stage::Button));
    assert(Transfer.Depart(Stage::Button, true));
    assert(Transfer.Arrive(Stage::Weight));
    assert(!Transfer.Depart(Stage::Weight, true));
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
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Underload, 1.0f);
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Uneven, 1.0f);
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.15f);
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.10f);
    assert(Balance.IsBalanced());
    Balance.Observe(Rules, PlayerLoad, 0.0f);
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 0.15f);
    Balance.Observe(Rules, Invalid, 0.0f);
    Balance.Observe(Rules, Target, 0.15f);
    assert(!Balance.IsBalanced());
    Balance.Observe(Rules, Target, 1.0f);
    assert(Balance.IsBalanced());
    Balance.Reset();
    assert(!Balance.IsBalanced());
    static_assert(ParseSegment("Balance") == Segment::Balance);
    static_assert(!IsCheckpoint(Segment::Balance) && !NextSegment(Segment::Balance));
}
}

int main()
{
    TestRouteAndCheckpoint();
    TestCircuitAndBallast();
    TestSupply();
    TestFallImpact();
    TestRadiationExposure();
    TestPowerSignalsAndOptics();
    TestGravityFields();
    TestPortalTransit();
    TestDev20Transfer();
    TestPlateBalance();
}
