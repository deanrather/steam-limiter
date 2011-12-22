@echo off

rem This constructs a subset of the GeoIP-to-ASN mapping database provided
rem by http://www.maxmind.com/app/asnum which is restricted to the specific
rem ISPs that we support filtering.

rem Since our query volume will be low, and the turnover of the netblocks
rem for these ISPs also low, the intent here is to avoid having to use a
rem full or very expensive GeoIP database when all we need is to be able to
rem do a quick mapping to decide which of the supported ISPs a call is
rem coming from.

rem Note that these ISPs can use more than one AS reference number, hence
rem the name match here; this is just a quick manual prefilter before I
rem decide how to convert the netblocks into some other form, be that data
rem for for loading into app engine or a set of Python literals.

rem Another handy thing to do is load the extracted CSV file into a Google
rem Docs spreadsheet to play with; I did this to confirm the specific ASN
rem codes used for the ISPs, since they sometimes have multiple assigned
rem numbers; mapping the ASxxx numbers to an internal ID seems simplest,

findstr /r "TelstraClear Orcon Snap.Internet Canterbury Victoria.University CallPlus" %1 >netblock.csv

rem Targeting these Australian ISPs for now since they seem the biggest and
rem some people from them have installed the limiter tool

findstr /r "Telstra.Pty iiNet Internode Optus Primus" %1 > netblock2.csv

rem Other Australian ISPs, some connected with iiNet various ways

findstr /r "Westnet.Internet Netspace.Online Adam.Internet" %1 > netblock3.csv

rem South Africa/Iceland - AS3741 is Internet Solutions LIR but it's in the
rem registry as plain "IS" which isn't helpful, AS12969 is Vodafone Iceland
rem under an Icelandic name

findstr /r "webafrica AS3741 AS12969" %1 > netblock4.csv
