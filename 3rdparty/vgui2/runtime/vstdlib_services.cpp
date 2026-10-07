// vstdlib layer for the static VGUI2 port
// A few entry points that BugfixedHL provides through its prebuilt vstdlib.

#include "vgui/VGUI2.h"
#include "vgui/IScheme.h"
#include "vgui_controls/Controls.h"

namespace vgui2
{

HScheme VGui_GetDefaultScheme()
{
	// The real default scheme is installed by the engine's ISchemeManager when
	// scheme files are loaded. Return a reserved sentinel for now.
	return (HScheme)1;
}

} // namespace vgui2