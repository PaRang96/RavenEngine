#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Entity.hpp"

#include <memory>

namespace Raven::ECS
{
    class Registry
    {
    public:
        RAVEN_API Registry();
        RAVEN_API ~Registry();

        RAVEN_API Entity Create();

        RAVEN_API bool Destroy(Entity entity) noexcept;

        RAVEN_API bool IsAlive(Entity entity) const noexcept;

        Registry(const Registry &) = delete;
        Registry &operator=(const Registry &) = delete;

        Registry(Registry &&) = delete;
        Registry &operator=(Registry &&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
