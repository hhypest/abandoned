#include "Application/Expedition/EntryCourse.h"
#include "Application/Puzzles/OpticsCourse.h"
#include "Domain/Expedition/SuitSupply.h"
#include "Domain/Puzzles/BallastRules.h"
#include "Domain/Puzzles/ChamberCircuit.h"

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
}

int main()
{
    TestRouteAndCheckpoint();
    TestCircuitAndBallast();
    TestSupply();
    TestPowerSignalsAndOptics();
}
