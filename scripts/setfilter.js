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
 * Simple wrapper for using the XHR object synchronously to perform a form POST
 * to send data to the GAE service.
 */

function simplePost (path) {
    if (path.substr (0, 7) !== "http://")
        path = base + path;

    xhr.open ("POST", path, false);
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

/**
 * Keep the current profile updated if it's set to update.
 */

function updateProfile () {
    var profile = shell.RegRead (regPath + "Profile");
    profile = String.fromCharCode (profile + 64) + "\\";

    try {
      var update = shell.RegRead (regPath + profile + "Update");

      /*
       * If we get here the value is set, so we can update the filter if the
       * location is still the same. If the profile is completely wrong, it's
       * debatable whether to correct it or not but for safety now I'm not
       * going to.
       */

      if (shell.RegRead (regPath + profile + "ISP") == bundle.ispname) {
        shell.RegWrite (regPath + profile + "Filter", bundle.filterrule);
        shell.RegWrite (regPath + profile + "Country", bundle.country);
      }
    } catch (e) {
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
} else if (hasArg ("test")) {
    /*
     * Point at an alternate version of the webservice for testing.
     */

    base = "http://2.steam-limiter.appspot.com/";
}

if (xhr == undefined)
    WScript.Quit (1);

/*
 * If asked to, send the current "custom" profile to the webservice.
 *
 * Attach the "country" field as a note, although the webservice will
 * see the incoming country via GAE's internal GeoIP lookup.
 */

if (hasArg ("upload")) {
    var profile = "C\\";
    try {
      var country = shell.RegRead (regPath + profile + "Country");
      var isp = shell.RegRead (regPath + profile + "ISP");
      var filter = shell.RegRead (regPath + profile + "Filter");

      simplePost ("uploadrule?ispname=" + escape (isp) +
                  "&filterrule=" + escape (filter) +
                  "&content=" + escape (country));
    } catch (e) {
    }

    WScript.Quit (0);
}

/*
 * Get all the data in a bundle; when developing the webservice I kept all the
 * services separate (and it's still handy that way so anyone interested can
 * poke at it by hand), but for now it's better to grab it all in one go.
 */

var getData = simpleGet ("all?cb");
var response = (getData && getData.responseText) || "null";
var bundle = fromJson (response);

if (hasArg ("show"))
    WScript.Echo (response);

if (! bundle || ! bundle.latest || ! bundle.filterip)
    WScript.Quit (2);

/*
 * If for some reason we're working with a down-level webservice that doesn't
 * send a filter rule...
 */

bundle.filterrule = bundle.filterrule || bundle.filterip;

if (hasArg ("upgrade")) {
    updateProfile ();

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

    var path = "%TEMP%\\package.exe";
    path = shell.ExpandEnvironmentStrings (path);
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
     * LastVersion/NextVersion, but I'm not perfectly sure about that just yet.
     */

    deleteFile (path);
    WScript.Quit (result);
}

/*
 * So, we're not upgrading and we're just setting things up
 */

shell.RegWrite (regPath + "NextVersion", bundle.latest);

/*
 * What happens next depends on whether we're being run by the installer, to
 * set up the "home" profile, or by the monitor applet to redetect the current
 * location.
 *
 * In the latter case, we always want to write to the "temp" profile, to let
 * the "cancel" option in the profile dialog work by not committing things (so
 * the "temp" profile is just a way to pass data back from the script to the
 * monitor applet, which then writes it where it needs to go).
 *
 * The other case like this is where we refresh just the "filter" from the
 * webservice if the profile is set to update, but that's taken care of above.
 */

var install = hasArg ("install");
var profile = install ? "A\\" : "D\\";

if (bundle.ispName !== "Unknown") {
    shell.RegWrite (regPath + profile + "Filter", bundle.filterrule);
    shell.RegWrite (regPath + profile + "ISP", bundle.ispname);
    shell.RegWrite (regPath + profile + "Country", bundle.country);
}

/*
 * Check and update the current profile as well, just in case.
 */

if (! install)
    updateProfile ();

/*
 * Aaaaaand, we're done.
 */

WScript.Quit (0);
