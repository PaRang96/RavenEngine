#pragma once

#include <string>

namespace Samples
{
	// The sample model with parts (docs/RenderingContentDesign.md, B7): a cart whose body, axles and four
	// wheels are separate nodes, as ASCII FBX. The wheels on both axles share their names, so the part
	// table tells them apart by their name paths ("FrontAxle/WheelLeft").
	std::string CreateCartModel();
}
