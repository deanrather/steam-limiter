/**@addtogroup AppEngine App Engine support for the Steam Limiter
 * @{@file
 *
 * Convert GeoASN data to Python script for GAE
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
/*
 * Simple Javascript script (for Windows Script Host) to convert the raw
 * GeoASN data from http://www.maxmind.com/app/asnum into a simple Python
 * literal for the ultra-simple mapping I want for my GAE service.
 */


/**
 * Set up access to the Script Host for access to file data.
 */

var fso = WScript.CreateObject ("Scripting.FileSystemObject");

/**
 * Read the content of the input file, line at a time.
 *
 * The point of this is to cheat and use eval () to get each CSV line in as
 * a simple Javascript array item as an easy way to parse it before starting
 * to match the data.
 *
 * Note that Singtel Optus have a large number of additional ASN's listed as
 * "Optus Customer Network" which are presumably netblocks managed by Optus
 * on behalf of corporate customers rather than consumer ones, and so which
 * we don't need to include.
 *
 * iINet, Netspace, Westnet and Adam are often referred to as a block, and the
 * Netspace name and AS is now assigned to iiNet in the APNIC registry, but
 * Westnet while being owned by iiNet still has a separate brand (and being
 * all the way over in WA, potentially different performance) and Adam
 * Internet's website appears to be quite separate from iiNet.
 *
 * For now I'll merge Netspace directly into iiNet but keep Westnet and Adam
 * separate. Westnet's and iiNet's netblocks are interthreaded quite finely so
 * if I do later merge them I'd want to actually merge the intervals.
 *
 * Canterbury, VUW, Lincoln and Christchurch Polytechnic all peer with Snap!
 * and all 3 universities have halls of residence, so I'll add them and skip
 * the Polytechnic for now.
 */

function readFile (file) {
    var result = [];
    var prev = { };
    var mapping = {
        "AS4768": 0,    /* TelstraClear */
        "AS7714": 0,
        "AS9901": 0,
        "AS17746": 1,   /* Orcon */
        "AS55454": 1,
        "AS23655": 2,   /* Snap! */
        "AS9432": 2,    /* University of Canterbury, access through Snap! */
                        /* http://www.it.canterbury.ac.nz/web/network/halls_connection.shtml */
                        /* also see http://bgp.he.net/AS9432 */
        "AS23905": 2,   /* Victoria University Wellington */
                        /* based on http://bgp.he.net/AS23905 */
        "AS38319": 2,   /* Lincoln University Canterbury */
                        /* based on http://bgp.he.net/AS38319 */

        "AS9790": 3,    /* CallPlus Services Limited aka Slingshot */
        "AS681": 4,     /* University of Waikato, a full class B and class C */
                        /* I am informed that some student services include
                         * unmetered Steam via Snap!, but Waikato is a special
                         * case all its own thanks to its large netblock.
                         */
        "AS17435": 5,   /* Xnet aka Worldxchange */
        "AS18119": 6,   /* ACSData, a non-retail outfit; according to the
                         * bgp.he.net data they only peer with TelstraClear in
                         * NZ but I've had one rule suggestion pointing at the
                         * Orcon Steam server...?
                         */
        "AS7657": 7,    /* Vodafone NZ Ltd */
        "AS2570": 8,    /* Telecom New Zealand */
        "AS4771": 8,    /* Netgate (another part of Telecom NZ) */
        "AS9325": 8,    /* Telecom XTRA */
        "AS17705": 9,   /* inspire.net.nz */

        "AS1221": 10,   /* Telstra */
        "AS4739": 11,   /* Internode */
        "AS4802": 12,   /* iiNet (Adelaide, SA) */
        "AS4854": 12,   /* Netspace Online Systems (Melbourne), now iiNet */
        "AS7474": 13,   /* SingTel Optus */
        "AS4804": 13,   /* Microplex PTY LTD, owned by Singtel Optus */
        "AS9443": 14,   /* iPrimus aka Primus Telecommunications */
        "AS9543": 15,   /* Westnet Internet Services (Perth, WA) */
        "AS9556": 16,   /* Adam Internet Pty Ltd (Adelaide, SA) */
        "AS18404": 17,  /* EAccess */

        /*
         * Another possible SA ISP is OpenWeb, but they don't seem to have
         * their own ASN, their website is hosted on MTN Business and I've had
         * an indication that their retail DSL detects as from the IS netblock.
         * Axxess are another such "virtual" ISP that is a pure reseller.
         */

        "AS3741": 30,   /* Internet Solutions (Johannesburg, South Africa) */
        "AS36943": 31,  /* webafrica (Cape Town, South Africa) */
        "AS5713": 32,   /* Telkom SAIX */
        "AS10474": 33,  /* MWeb */
        "AS12258": 33,

        "AS12969": 40,  /* Vodafone Iceland */

        /*
         * Google have a few ASNs but while I've seen a few installs from them
         * they tend to be in the 15169 block.
         *
         * Interestingly, Google do have a signficant presence in Sydney and
         * the Google servers I tend to end up using are in this block.
         */

        "AS15169": 50,  /* Google Inc. */

        /*
         * I've seen a few installs from Comcast, but they have a LOT of ASNs
         * and presumably they represent some geographical diversity inside the
         * US. So, I'll add their netblocks over time as I learn more about how
         * to handle Comcast.
         */

        "AS7922": 60,   /* actually seen this one used */
        "AS21508": 60,  /* actually seen this one used */
        "AS33490": 60   /* actually seen this one used */

/* Holding off on enabling all of these until I know more
        "AS7015": 60,
        "AS7016": 60,
        "AS7725": 60,
        "AS13367": 60,
        "AS20214": 60,
        "AS22258": 60,
        "AS23266": 60,
        "AS33287": 60,
        "AS33489": 60,
        "AS33491": 60,
        "AS33650": 60,
        "AS33651": 60,
        "AS33652": 60,
        "AS33653": 60,
        "AS33654": 60,
        "AS33655": 60,
        "AS33656": 60,
        "AS33657": 60,
        "AS33660": 60,
        "AS33661": 60,
        "AS33662": 60,
        "AS33663": 60,
        "AS33664": 60,
        "AS33665": 60,
        "AS33666": 60,
        "AS33668": 60,
        "AS36732": 60,
        "AS36733": 60
*/
    };

    var getId = RegExp ().compile ("AS[0-9]+");

    for (;;) {
        if (file.AtEndOfStream)
          return result;

        var line = file.ReadLine ();
        if (line === "")
            return result;

        var asid = line.match (getId)
        var id = mapping [asid];

        if (id === undefined)
            continue;

        /*
         * The CSV syntax uses double-doublequotes, convert that to the C-style
         * escape form for eval () to use.
         */

        for (;;) {
            quote = line.replace ("\"\"", "\\\"");
            if (quote === line)
                break;
            line = quote;
        }

        values = eval ("[" + line + "]");

        if (values.length < 3 || values [2] === null)
            continue;

        var range = { start: values [0], end: values [1], id: id };

        /*
         * If the new range and the previous can be merged, merge them.
         */

        if (prev && prev.end + 1 === range.start && prev.id === id) {
            WScript.Echo ("Merging");
            range.start = prev.start;
            result.pop ();
        }

        result.push (range);
        prev = range;
    }
}

var file = fso.OpenTextFile (WScript.Arguments (0));
var array = readFile (file);

file = fso.CreateTextFile ("ip_match.py");

file.WriteLine ("# This file is autogenerated by makeip2isp.js - do not edit");
file.WriteLine ("ip_table = [");

var key;
for (key in array) {
    var item = array [key];
    if (key > 0)
        file.WriteLine (",")

    /*
     * Originally I checked that the netblock range was aligned on a class C
     * boundary, but there's a *really* odd set of netblocks where Telstra and
     * Google interleave at the level of 2-3 hosts around 1208927796
     *
     * The most likely cause of this is that these IPs are ones Google lease
     * from Telstra, which have come under their ASN later on. Possibly this is
     * Google's Sydney office.
     */

    file.Write ("    (" + item.start + ", " + item.end + ", " + item.id + ")")
}

file.WriteLine ();
file.WriteLine ("];");

