#include "resource_manager.h"
#include "resource_manifest.h"


bool ResourceManager::file_path_from_UUID(const se::UUID& uuid, ResourcePath& filePath) const
{
    // Default manifest is at 0th index but all other take priority since Default manifest could
    // contain obsolete data.
    for(auto iter = m_resource_manifests.rbegin(); iter != m_resource_manifests.rend(); ++iter)
    {
        if((*iter)->uuid_to_file_path(uuid, filePath))
            return true;
    }

    return false;
}
namespace se {

}
