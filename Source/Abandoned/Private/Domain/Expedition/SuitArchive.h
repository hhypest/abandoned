#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace Abandoned::Domain::Expedition
{
enum class SuitRecord { ForecastSynchronized, Decontaminated, OxygenRestored };

class SuitArchive
{
public:
    std::size_t Size() const { return Count; }

    std::optional<SuitRecord> At(std::size_t Index) const
    {
        return Index < Count ? std::optional{Records[Index]} : std::nullopt;
    }

    bool Contains(SuitRecord Record) const
    {
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            if (Records[Index] == Record)
            {
                return true;
            }
        }
        return false;
    }

    void Reveal(SuitRecord Record)
    {
        if (!Contains(Record) && Count < Records.size())
        {
            Records[Count++] = Record;
        }
    }

private:
    std::array<SuitRecord, 3> Records{};
    std::size_t Count = 0;
};
}
