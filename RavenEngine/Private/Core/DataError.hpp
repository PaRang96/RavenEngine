#pragma once

#include <stdexcept>

namespace Raven
{
	// A data or capacity error that escapes to a host stage (docs/DataModelDesign.md, FD-06): bad
	// authored or game-written values, missing content, or a full table. Every stage boundary
	// recovers it, including Render, which recovers nothing else. Engine API misuse stays a
	// std::logic_error and ends the process. Existing std::logic_error-family throws of data
	// conditions are converted when their code is next touched.
	class DataError : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};
}
