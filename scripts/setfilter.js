/**@addtogroup Install Installation/update helpers
 * @{@file
 *
 * Fetch the right steam filter IP to use.
 *
 * @author Nigel Bree <nigel.bree@gmail.com>
 *
 * Copyright (C) 2011 Nigel Bree; All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Crude way of checking for arguments.
 *
 * Obviously, named arguments are easier to look for but WSH makes them tedious
 * to actually use, so waste a little CPU on making the command line friendlier.
 */

function hasArg (name) {
    var args = WScript.Arguments.Unnamed;
    var i;
    for (i = 0 ; i < args.length ; ++ i) {
        if (args (i) === name)
            return true;
    }
    return false;
}

/**
 * Use the XmlHttpRequest script interface to fetch some data from the
 * web service at http://steam-limiter.appspot.com such as the name of the
 * upstream ISP (if it's one we support) and the default IP to filter.
 *
 * I could hide these in a closure, but why bother for such a small script?
 */

var xhr = WScript.CreateObject ("MSXML2.XmlHttp");
var shell = WScript.CreateObject ("WScript.Shell");
var base = "http://steam-limiter.appspot.com/";
var regPath = "HKCU\\Software\\SteamLimiter\\"

/**
 * Include the requesting app version and WScript version in the user-agent
 * string so I can get a rough idea what old versions are around as I roll the
 * limiter app forward; thisalso gives me useful options in the webservice to
 * handle back-compatibility for request URLs by using the user-agent string
 * to return results formatted for older versions.
 */

var version;
try {
  version = shell.RegRead (regPath + "LastVersion");
} catch (e) {
  version = "0.4.1.0";
}
version = version + '(WScript ' + WScript.Version + ')';

/**
 * Simple wrapper for using the XHR object synchronously to get data from the
 * GAE service (or elsewhere, if an explicit path is present)
 */

function simpleGet (path) {
    if (path.substr (0, 7) !== "http://")
        path = base + path;

    xhr.open ("GET", path, false);
    xhr.setRequestHeader ("User-Agent", "steam-limiter/" + version);

    try {
        xhr.send ();
    } catch (e) {
        return null;
    }

    return xhr;
}

/**
 * Simple JSON extractor for Windows Script. There's no native JSON here, but
 * the webservice is trustworthy so we just do some really basic sanitization.
 */

function fromJson (text) {
    /*
     * Remove anything that's quoted; check the residual unquoted data for any
     * things that could be undesirable.
     *
     * Mainly, that's () or =. Some sanitisers are picky about JSON's syntax,
     * but kinda excessively so; per Postel I choose to be more liberal, and as
     * = or () are the things that actually can have negative effects we'll
     * blacklist those explicitly rather than trying to whitelist.
     */

    var nontext = text.replace (/"(\\.|[^"\\])*"/g, "")
                      .replace (/'(\\.|[^'\\])*'/g, "");

    if (/[()=]/.test (nontext))
        return undefined;

    return eval ("(" + text + ")");
}

/**
 * Try fetching the path to the ideal download and then fetching the
 * actual file. When this works the responseBody is a SAFEARRAY of bytes
 * which Windows Script Host JScript can't actually manipulate, but it
 * is happy to hold the reference so we can pass it to an ADODB.Stream
 * object to write the file.
 */

function download (url, path) {
    var getFile = simpleGet (url);

    var stream = WScript.CreateObject ("ADODB.Stream");
    var typeBinary = 1
    stream.Type = typeBinary;
    stream.Open ();
    stream.Write (getFile.responseBody);

    var overwrite = 2
    stream.SaveToFile (path, overwrite);
}

/**
 * Delete a file; tedious because of the way FSO reports errors, and the way
 * Windows executables can stay in-use even though the process using it has
 * exited.
 */

function deleteFile (path) {
    var fso = WScript.CreateObject ("Scripting.FileSystemObject");

    for (;;) {
        try {
            fso.DeleteFile (path);
            return 0;
        } catch (e) {
            if (e.number !== 0x800A0046)
                return 1;
        }

        WScript.Sleep (100);
    }
}


/*
 * Grab a bunch of data from the webservice; ask for it in JSON, do a simple
 * sanitise and eval.
 */

if (hasArg ("debug")) {
    /*
     * Point at a local instance running in the App Engine SDK.
     */

    base = "http://localhost:8080/";
}

if (xhr == undefined)
    WScript.Quit (1);

/*
 * Get all the data in a bundle; when developing the webservice I kept all the
 * services separate (and it's still handy that way so anyone interested can
 * poke at it by hand), but for now it's better to grab it all in one go.
 */

var getData = simpleGet ("all?cb");
var response = getData && getData.responseText || "";
var bundle = fromJson (response);

if (hasArg ("show"))
    WScript.Echo (response);

if (! bundle || ! bundle.latest || ! bundle.filterip)
    WScript.Quit (2);

if (hasArg ("upgrade")) {
    var current = shell.RegRead (regPath + "LastVersion")
    if (current === bundle.latest)
        WScript.Quit (0);

    /*
     * Before downloading the update package, verify that it's coming
     * from a Google Code URL; this costs some future flexibility but
     * it's just a safer thing to do.
     */

    if (! /^http:\/\/.*\.googlecode\.com\//.test (bundle.download))
        WScript.Quit (3);

    var path = "package.exe";
    download (bundle.download, path);

    /*
     * Run the downloaded package silently to upgrade.
     *
     * This seems to have a habit of returning 1223, perhaps because of all the
     * UAC shenanigans that need to happen for Vista/Win7.
     */

    var window = 2;
    var result = shell.Run (path + " /S", window, true);

    /*
     * We don't need the upgrade package any more, clean it out.
     *
     * In principle it might be a good idea to check here what has happened to
     * LastVersion/NextVersion, but I'm not just yet.
     */

    deleteFile (path);
    WScript.Quit (result);
}

/*
 * So, we're not upgrading and we're just setting things up.
 */

shell.RegWrite (regPath + "NextVersion", bundle.latest);

var setup = hasArg ("setup");

/*
 * Even if we're not explicitly asked to do setup, if there isn't an existing
 * registry setting take care of it.
 */

if (! setup) {
    var server;
    try {
        server = shell.RegRead (regPath + "Server");
    } catch (e) {
    }
    setup = server === undefined;
}

if (setup && bundle.ispName !== "Unknown") {
    shell.RegWrite (regPath + "Server", bundle.filterip);
    shell.RegWrite (regPath + "ISP", bundle.ispname);
}

/*
 * Aaaaaand, we're done.
 */

WScript.Quit (0);
