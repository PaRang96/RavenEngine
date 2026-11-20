#pragma once

#include <cstdint>

namespace Raven { class Scene; }

namespace Raven::ECS
{
    class Registry;

    class Entity
    {
    public:
        constexpr Entity() noexcept = default;
        constexpr bool IsNull() const noexcept
        {
            return m_Registry == 0;
        }
        constexpr bool operator==(const Entity& other) const noexcept = default;

    private:
        friend class Registry;
        friend class ::Raven::Scene;

        constexpr Entity(std::uint64_t registryId,
                         std::uint32_t index,
                         std::uint32_t generation) noexcept
            : m_Registry(registryId),
              m_Index(index),
              m_Generation(generation)
        {
        }

        std::uint64_t m_Registry = 0;
        std::uint32_t m_Index = 0;
        std::uint32_t m_Generation = 0;
    };
}
