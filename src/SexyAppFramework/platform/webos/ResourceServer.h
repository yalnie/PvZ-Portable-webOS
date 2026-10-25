#ifndef __WEBOS_RESOURCE_SERVER_H__
#define __WEBOS_RESOURCE_SERVER_H__

namespace Sexy
{

// Returns true if game resources (main.pak >= 1MB and properties/resources.xml) exist.
bool WebOSHasGameResources();

// Runs the setup HTTP server with a random port, displays setup instructions on the TV,
// and waits until the user uploads resources and triggers "Start Game".
// Returns true when resources are installed and ready to play.
// Returns false if the user pressed Back / exited.
bool WebOSRunResourceServer();

}

#endif // __WEBOS_RESOURCE_SERVER_H__
