/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or 
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/* nsDll
 *
 * Abstraction of a Dll. Stores modifiedTime and size for easy detection of
 * change in dll.
 *
 * dp Suresh <dp@netscape.com>
 */

#include "xcDll.h"
#include "nsDebug.h"
#include "nsIComponentManager.h"
#include "nsIComponentLoaderManager.h"
#include "nsIModule.h"
#include "nsILocalFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsString.h"
#include "nsITimelineService.h"
#ifdef DEBUG
#if defined(VMS)
#include <lib$routines.h>
#include <ssdef.h>
#elif defined(XP_MACOSX)
#include <signal.h>
#endif
#endif /* defined(DEBUG) */

#include "nsNativeComponentLoader.h"

extern nsresult NS_GetComponentLoaderManager(nsIComponentLoaderManager* *result);

nsDll::nsDll(nsIFile *dllSpec, 
             const char *registryLocation)
  : 
    m_dllSpec(do_QueryInterface(dllSpec)),
    m_instance(NULL), 
    m_status(DLL_OK), 
    m_moduleObject(NULL),
    m_markForUnload(PR_FALSE)
{
    NS_ASSERTION(registryLocation, "registryLocation is null");

    m_registryLocation = nsCRT::strdup(registryLocation);
}

nsDll::~nsDll(void)
{
#if DEBUG_dougt
    // The dll gets deleted when the dllStore is destroyed. This happens on
    // app shutdown. At that point, unloading dlls can cause crashes if we have
    // - dll dependencies
    // - callbacks
    // - static dtors
    // Hence turn it back on after all the above have been removed.
    Unload();
#endif
    if (m_registryLocation)
        nsCRT::free(m_registryLocation);
}

const char *
nsDll::GetDisplayPath()
{
    if (m_registryLocation)
        return m_registryLocation;    
    return "unknown!";
}

PRBool
nsDll::HasChanged()
{
    nsCOMPtr<nsIComponentLoaderManager> manager;
    NS_GetComponentLoaderManager(getter_AddRefs(manager));
    if (!manager)
        return PR_TRUE;

    // If mod date has changed, then dll has changed
    PRInt64 currentDate;
    nsresult rv = m_dllSpec->GetLastModifiedTime(&currentDate);
    if (NS_FAILED(rv))
        return PR_TRUE;
    PRBool changed = PR_TRUE;
    manager->HasFileChanged(m_dllSpec, nsnull, currentDate, &changed); 
    return changed;
}

PRBool nsDll::Load(void)
{
	if (m_status != DLL_OK)
	{
		return (PR_FALSE);
	}
	if (m_instance != NULL)
	{
		// Already loaded
		return (PR_TRUE);
	}

    if (m_dllSpec)
    {
#ifdef NS_BUILD_REFCNT_LOGGING
        nsTraceRefcnt::SetActivityIsLegal(PR_FALSE);
#endif
        
    // Load any library dependencies
    //   The Component Loader Manager may hold onto some extra data
    //   set by either the native component loader or the native 
    //   component.  We assume that this data is a space delimited
    //   listing of dependent libraries which are required to be
    //   loaded prior to us loading the given component.  Once, the
    //   component is loaded into memory, we can release our hold 
    //   on the dependent libraries with the assumption that the 
    //   component library holds a reference via the OS so loader.

    nsCOMPtr<nsIComponentLoaderManager> manager;
    NS_GetComponentLoaderManager(getter_AddRefs(manager));
    if (!manager)
        return PR_TRUE;

#if defined(LINUX)        
    nsXPIDLCString extraData;
    manager->GetOptionalData(m_dllSpec, m_registryLocation, getter_Copies(extraData));
    
    nsVoidArray dependentLibArray;

    // if there was any extra data, treat it as a listing of dependent libs
    if (extraData != nsnull) 
    {

        // all dependent libraries are suppose to be in the "gre" directory.
        // note that the gre directory is the same as the "bin" directory, 
        // when there isn't a real "gre" found.

        nsXPIDLCString path;
        nsCOMPtr<nsIFile> file;
        NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(file));
        
        if (!file)
            return NS_ERROR_FAILURE;

        // we are talking about a file in the GRE dir.  Lets append something
        // stupid right now, so that later we can just set the leaf name.
        file->AppendNative(NS_LITERAL_CSTRING("dummy"));

        char *buffer = strdup(extraData); 
        if (!buffer)
            return NS_ERROR_OUT_OF_MEMORY;

        char* newStr;
        char *token = nsCRT::strtok(buffer, " ", &newStr);
        while (token!=nsnull)
        {
            nsXPIDLCString libpath;
            file->SetNativeLeafName(nsDependentCString(token));
            file->GetNativePath(path);
            if (!path)
                return NS_ERROR_FAILURE;

            // Load this dependent library with the global flag and stash 
            // the result for later so that we can unload it.
            PRLibSpec libSpec;
            libSpec.type = PR_LibSpec_Pathname;

            // if the depend library path starts with a / we are 
            // going to assume that it is a full path and should
            // be loaded without prepending the gre diretory 
            // location.  We could have short circuited the 
            // SetNativeLeafName above, but this is clearer and
            // the common case is a relative path.

            if (token[0] == '/')
                libSpec.value.pathname = token;
            else
                libSpec.value.pathname = path;
            
            PRLibrary* lib = PR_LoadLibraryWithFlags(libSpec, PR_LD_LAZY|PR_LD_GLOBAL);
            // if we couldn't load the dependent library.  We did the best we
            // can.  Now just let us fail later if this really was a required
            // dependency.
            if (lib) 
                dependentLibArray.AppendElement((void*)lib);
                
            token = nsCRT::strtok(newStr, " ", &newStr);
        }
        free(buffer);
    }
#endif
    // load the component

    nsCOMPtr<nsILocalFile> lf(do_QueryInterface(m_dllSpec));
    NS_ASSERTION(lf, "nsIFile here must implement a nsILocalFile"); 
    lf->Load(&m_instance);

#if defined(LINUX) 
    // Unload any of library dependencies we loaded earlier. The assumption  
    // here is that the component will have a "internal" reference count to
    // the dependency library we just loaded.  
    // XXX should we unload later - or even at all?

    if (extraData != nsnull)
    {
        PRInt32 arrayCount = dependentLibArray.Count();
        for (PRInt32 index = 0; index < arrayCount; index++)
            PR_UnloadLibrary((PRLibrary*)dependentLibArray.ElementAt(index));
    }
#endif

#ifdef NS_BUILD_REFCNT_LOGGING
        nsTraceRefcnt::SetActivityIsLegal(PR_TRUE);
        if (m_instance) {
            // Inform refcnt tracer of new library so that calls through the
            // new library can be traced.
            nsCAutoString displayPath;
            m_dllSpec->GetNativePath(displayPath);
            nsTraceRefcnt::LoadLibrarySymbols(displayPath.get(), m_instance);
        }
#endif
    }

#ifdef SHOULD_IMPLEMENT_BREAKAFTERLOAD
    // Debugging help for components. Component dlls need to have their
    // symbols loaded before we can put a breakpoint in the debugger.
    // This will help figureing out the point when the dll was loaded.
    BreakAfterLoad(GetDisplayPath());
#endif

    return ((m_instance == NULL) ? PR_FALSE : PR_TRUE);
}

PRBool nsDll::Unload(void)
{
	if (m_status != DLL_OK || m_instance == NULL)
		return (PR_FALSE);

    // Shutdown the dll
    Shutdown();

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcnt::SetActivityIsLegal(PR_FALSE);
#endif
	PRStatus ret = PR_UnloadLibrary(m_instance);
#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcnt::SetActivityIsLegal(PR_TRUE);
#endif

	if (ret == PR_SUCCESS)
	{
		m_instance = NULL;
		return (PR_TRUE);
	}
	else
		return (PR_FALSE);
}

void * nsDll::FindSymbol(const char *symbol)
{
	if (symbol == NULL)
		return (NULL);
	
	// If not already loaded, load it now.
	if (Load() != PR_TRUE)
		return (NULL);

    return(PR_FindSymbol(m_instance, symbol));
}


// Component dll specific functions
nsresult nsDll::GetDllSpec(nsIFile **fsobj)
{
    NS_ASSERTION(m_dllSpec, "m_dllSpec NULL");
    NS_ASSERTION(fsobj, "xcDll::GetModule : Null argument" );

    *fsobj = m_dllSpec;
    NS_ADDREF(*fsobj);
    return NS_OK;
}

nsresult nsDll::GetModule(nsISupports *servMgr, nsIModule **cobj)
{
    nsCOMPtr<nsIComponentManager> compMgr;
    nsresult rv = NS_GetComponentManager(getter_AddRefs(compMgr));
    NS_ASSERTION(compMgr, "Global Component Manager is null" );
    if (NS_FAILED(rv)) return rv;

    NS_ASSERTION(cobj, "xcDll::GetModule : Null argument" );

    if (m_moduleObject)
    {
        NS_ADDREF(m_moduleObject);
        *cobj = m_moduleObject;
        return NS_OK;
    }

	// If not already loaded, load it now.
	if (Load() != PR_TRUE) return NS_ERROR_FAILURE;

    // We need a nsIFile for location
    if (!m_dllSpec)
    {
        return NS_ERROR_FAILURE;
    }

    nsGetModuleProc proc =
      (nsGetModuleProc) FindSymbol(NS_GET_MODULE_SYMBOL);

    if (proc == NULL)
        return NS_ERROR_FACTORY_NOT_LOADED;

    rv = (*proc) (compMgr, m_dllSpec, &m_moduleObject);
    if (NS_SUCCEEDED(rv))
    {
        NS_ADDREF(m_moduleObject);
        *cobj = m_moduleObject;
    }
    return rv;
}


// These are used by BreakAfterLoad, below.
#ifdef SHOULD_IMPLEMENT_BREAKAFTERLOAD
static nsCString *sBreakList[16];
static int sBreakListCount = 0;
#endif

nsresult nsDll::Shutdown(void)
{
    // Release the module object if we got one
    nsrefcnt refcnt;
    if (m_moduleObject)
    {
        NS_RELEASE2(m_moduleObject, refcnt);
        NS_ASSERTION(refcnt == 0, "Dll moduleObject refcount non zero");
    }
#ifdef SHOULD_IMPLEMENT_BREAKAFTERLOAD
    for (int i = 0; i < sBreakListCount; i++)
    {
        delete sBreakList[i];
        sBreakList[i] = nsnull;
    }
    sBreakListCount = 0;
#endif
    return NS_OK;

}
#ifdef SHOULD_IMPLEMENT_BREAKAFTERLOAD
void nsDll::BreakAfterLoad(const char *nsprPath)
{
    static PRBool firstTime = PR_TRUE;

    // return if invalid input
    if (!nsprPath || !*nsprPath) return;

    // return if nothing to break on
    if (!firstTime && sBreakListCount == 0) return;

    if (firstTime)
    {
        firstTime = PR_FALSE;
        // Form the list of dlls to break on load
        nsCAutoString envList(getenv("XPCOM_BREAK_ON_LOAD"));
        if (envList.IsEmpty()) return;
        PRInt32 ofset = 0;
        PRInt32 start = 0;
        do
        {
            ofset = envList.FindChar(':', start);
            sBreakList[sBreakListCount] = new nsCString();
            envList.Mid(*(sBreakList[sBreakListCount]), start, ofset);
            sBreakListCount++;
            start = ofset + 1;
        }
        while (ofset != -1 && 16 > sBreakListCount); // avoiding vc6.0 compiler issue. count < thinks it is starting a template
    }

    // Find the dllname part of the string
    nsCString currentPath(nsprPath);
    PRInt32 lastSep = currentPath.RFindCharInSet(":\\/");

    for (int i=0; i<sBreakListCount; i++)
        if (currentPath.Find(*(sBreakList[i]), PR_TRUE, lastSep) > 0)
        {
            // Loading a dll that we want to break on
            // Put your breakpoint here
            fprintf(stderr, "...Loading module %s\n", nsprPath);
            // Break in the debugger here.
#if defined(linux) && defined(__i386)
            asm("int $3");
#elif defined(VMS)
            lib$signal(SS$_DEBUG);
#elif defined(XP_MACOSX)
            raise(SIGTRAP);
#endif
        }
    return;
}
#endif /* SHOULD_IMPLEMENT_BREAKAFTERLOAD */
