# Rules for ISP data returned from the old version of the webservice API
#
# Newer versions past 0.7.1.0 use subtly different URLs so I can leave this
# in place for users of old versions. Those users will typically also see a
# different set up upgrade information, so that they don't get told about
# upgrades past 0.7.1.0 due to Google Code retiring their download system.
#
# The main thing improved in 0.7.1.0 aside from changes to the upgrading
# process is that host rewrite rules now work better (earlier versions had
# some subtle bugs), and I've changed how the default built-in rules in the
# filter.dll work for "CS" type hosts work so that 0.7.1.0 and later now is
# set up so that accesses to "CS" type can be rewritten to standard servers.
# Because this all means quite different rules become possible, and in
# particular it becomes a bit easier to write rules for hosts which are just
# Squid-type proxies (which ISPs like because they are trivial to install
# and managed and there's just one IP for them to unmeter).

isps = {
    - 1: { 'name': 'Unknown', 'server': '203.167.129.4',
           'filter': '# No specific content server for your ISP' },

    # Note that most NZ Universities appear to have peering with and/or student
    # internet provided via Snap! - most I've lumped in as part of Snap! but
    # Waikato is a special case having an old netblock with a full class B and
    # it is being set as its own case, just using the same rules as Snap! for
    # now. I'll call it Lightstream (which is a semi-commercial spinoff used
    # for student internet) since that's probably most useful.

    # Note that aside from most NZ ISPs not generally understanding the concept
    # of giving things like servers DNS names, pretty much all of these are
    # filtered so I can't detect whether they support port 80 or not, and none
    # of the ISPs document this properly.

    # TelstraClear have made wlgwpstmcon01.telstraclear.co.nz go away, but the
    # new name is steam.cdn.vodafone.co.nz - Valve have actually just
    # started to advertise this server as steam.cdn.vodafone.co.nz, so I'm also
    # allowing it that way but it appears to not be blocking requests via the
    # content*.steampowered.com name and so it's all good! It appears that
    # despite no official announcement, they've actually done something right.

    0: { 'name': 'TelstraClear New Zealand', 'server': '203.167.129.4',
         'filter': '*:27030=steam.cdn.vodafone.co.nz;content*.steampowered.com=steam.cdn.vodafone.co.nz',
         'allow': '//steam.cdn.vodafone.co.nz=*' },

    1: { 'name': 'Orcon New Zealand', 'server': '0.0.0.0',
         'filter': '# Orcon no longer have a Steam server' },
    2: { 'name': 'Snap! New Zealand', 'server': '0.0.0.0',
         'filter': '# Snap! no longer have a Steam server' },
    3: { 'name': 'Slingshot New Zealand', 'server': '119.224.142.146',
         'filter': '*:27030=119.224.142.146' },
    4: { 'name': 'Lightstream, Waikato New Zealand', 'server': '202.124.127.66',
         'filter': '*:27030=202.124.127.66' },
    5: { 'name': 'Xnet/WorldxChange New Zealand', 'server': '58.28.25.146',
         'filter': '# XNet no longer have a Steam server' },
    6: { 'name': 'ACSData, Wellington NZ', 'server': '0.0.0.0',
         'filter': '# No known unmetered Steam server' },
    7: { 'name': 'Vodafone New Zealand', 'server': '0.0.0.0',
         'filter': '# steam.cdn.vodafone.co.nz is coming soon!' },
    8: { 'name': 'Telecom/XTRA New Zealand', 'server': '0.0.0.0',
         'filter': '# No known unmetered Steam server' },
    9: { 'name': 'InSPire New Zealand', 'server': '0.0.0.0',
         'filter': '# Please contribute a server IP for InSPire' },

    # For the Australian ISPs I'm using two servers per ISP to start but the
    # ideal lists here are a bit hard to figure, since there are a mix of
    # filtered and non-filtered servers, and thanks to peering often customers
    # of one ISP can get optimal service from a different peer.
    # It's hard to tell whether Westnet's netblock really should be merged into
    # the iiNet one or not, but I'm keeping that separate for now.

    # Telstra's GameArena servers used to be be classic Filtered servers with
    # no visible HTTP support. This has changed as of this announcement:
    # http://forums.gamearena.com.au/suggestionsfeedback/topic/159928-new-steam-content-system-now-available-on-gamearena
    # in which supposedly 203.39.198.136 is now the only unmetered server
    # (it's hard to make sense of that thread since there's conflicting
    # information there and you can't tell who is official Telstra and
    # who's just some random person).
    # A later follow-up on that thread finally documented a DNS name for
    # their servers; the Telstra DNS doesn't include a reverse lookup
    # from the IP to the name since they are pretty incompetent, but the
    # name appears to be real and I'm tossing in an 'allow' filter for
    # that host since I'm guessing that's the primary way it's advertised
    # and that allowing it will permit more download parallelism.

    10: { 'name': 'Telstra BigPond Australia', 'server': '0.0.0.0',
          'filter': '*:27030=steam.content.bigpondgames.com;' +
                    'content*.steampowered.com=steam.content.bigpondgames.com',
          'allow': '//steam.content.bigpondgames.com=*' },

    # For a long time the iiNet rule was only these three specific servers:
    # *:27030=steam1.filearena.net,steam-wa.3fl.net.au,steam-nsw.3fl.net.au
    #
    # Internode's and iiNet's rules appear to be broken. Supposedly all the
    # iiNet steam content is on steam.cdn.on.net but that does not appear to
    # work for me or some Internode customers. The only two servers on the
    # Internode master list - the only list they provide, there is no list
    # of just Steam servers -
    # http://www.internode.on.net/residential/entertainment/unmetered_content/ip_address_list/
    # that I can confirm *are* steam servers are 49.143.234.14 and
    # files-oc-syd.games.on.net - those are the only individual IPs on that
    # list that appear to be steam content servers.  There may be more, but
    # if so they are just undocumented entries in some of the larger netblocks
    # listed on that page and I can't scan them all by hand.

    11: { 'name': 'Internode Australia', 'server': '0.0.0.0',
          'filter': '*:27030=49.143.234.14,files-oc-syd.games.on.net;' +
                    'content*.steampowered.com=49.143.234.14,files-oc-syd.games.on.net',
          'allow': '//steam.cdn.on.net=*' },

    # iiNet are now special, because their Steam server (which is behind a
    # front-end like the Telstra one) returns 403 access errors to me but is
    # accessible to subscribers, and it appears to want its host: entry to be
    # itself and rejects the *.steampowered.com domain names flat out.

    12: { 'name': 'iiNet Australia', 'server': '0.0.0.0',
          'filter': '*:27030=steam.cdn.on.net;' +
                    'content*.steampowered.com=',
          'allow': '//steam.cdn.on.net=*' },

    # Evidently Optus actually don't actually offer any unmetered content, so
    # these server selections are intended more for download performance than
    # for providing unmetered data. I've a report from an Optus customer that
    # the Sydney and San Jose servers give much better perf than the two which
    # were previously listed here. The 49.xx.xx.xx servers are on AS209 QWest
    # so are in the United States, so the Sydney server should work well.
    #
    # The hard bit about this is that performance depends on load, and so the
    # server selections that work well most of the time may end up being less
    # than optimal during load spikes such as Steam sales.
    #
    # Whether I should try and shoot down steam.ix.asn.au here via DNS is not
    # clear, but for safety I will try valve.tge2-3.fr4.syd.llnw.net since that
    # is at least unmetered (at worst it'll 404 due to virtual hosting). Since
    # Optus don't support on.net either, I'll do that same for that.

    13: { 'name': 'Optus Australia', 'server': '0.0.0.0',
          'filter': '# Optus do not have any unmetered Steam servers' },

    # Angus Wolfcastle pointed out http://www.ipgn.com.au/Support/Support/Steam
    # where iPrimus list their unmetered servers. That page has now gone and I
    # have no information on iPrimus now, and as with iiNet and Internode most
    # of the old Steam servers are now completely gone.

    14: { 'name': 'iPrimus Australia', 'server': '0.0.0.0',
          'filter': '*:27030=49.143.234.14;' +
                    'content*.steampowered.com=steam.ix.asn.au,steam01.qld.ix.asn.au,steam01.vic.ix.asn.au;' +
                    '*.cs.steampowered.com=valve217.cs.steampowered.com',
          'allow': '//*.ix.asn.au=*;//*.steampowered.com=*' },

    # As with iPrimus since many of the old Steam servers listed as unmetered
    # are now no longer active, try using the current Internode rules

    15: { 'name': 'Westnet Internet Services (Perth, WA)', 'server': '0.0.0.0',
          'filter': '*:27030=49.143.234.14,files-oc-syd.games.on.net;' +
                    'content*.steampowered.com=49.143.234.14,files-oc-syd.games.on.net',
          'allow': '//steam.cdn.on.net=*' },

    # Adam appear to have a list of servers (unfortunately, not DNS names and also
    # unfortunately, no indication which ones serve HTTP content).
    # http://www.adam.com.au/support/downloads/unmetered_ip_address_list.txt
    # The on.net Steam servers are also generally claimed as unmetered, but not
    # the WAIX ones which Valve's CDN will sometimes try and force so that needs
    # to be redirected to steam.cdn.on.net through DNS to steam.cdn.on.net, as
    # that fortunately isn't sensitive to the Host: presented (thanks to WP user
    # networkMe for his immense help in diagnosing all this).

    16: { 'name': 'Adam Internet (Adelaide, SA)', 'server': '0.0.0.0',
          'filter': '*:27030=49.143.234.14,files-oc-syd.games.on.net;' +
                    'content*.steampowered.com=49.143.234.14,files-oc-syd.games.on.net',
          'allow': '//steam.cdn.on.net=*' },

    17: { 'name': 'EAccess Broadband, Australia', 'server': '0.0.0.0',
          'filter': '# No known unmetered Steam server' },

    # Slots 18-29 are reserved for future Australian ISPs or tertiary institutions.

    # Because it seems customers with dual ISP accounts is common in South
    # Africa (along with a large fraction of the retail ISPs being pure
    # resellers), detection in ZA needs extra work from the client side to
    # be sure of what connectivity is present, so there are rule extensions
    # to detect dual-ISP situations and prefer the WebAfrica unmetered server
    # if there's connectivity to the WebAfrica customer side.

    30: { 'name': 'Internet Solutions (Johannesburg, South Africa)', 'server': '196.38.180.3',
          'filter': '*:27030=steam.isgaming.co.za',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/IS dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za'
                  }
              }
          }
        },
    31: { 'name': 'webafrica (Cape Town, South Africa)', 'server': '41.185.24.21',
          'filter': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za' },
    32: { 'name': 'Telkom SAIX, South Africa', 'server': '0.0.0.0',
          'filter': '# No known unmetered Steam server',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/SAIX dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za'
                  }
              }
          }
        },
    33: { 'name': 'MWeb, South Africa', 'server': '196.28.69.201',
          'filter': '*:27030=196.28.69.201,196.28.169.201',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/MWeb dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za'
                  }
              }
          }
        },
    34: { 'name': 'Cybersmart, South Africa', 'server': '0.0.0.0',
          'filter': '# No known Steam server for Cybersmart',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/Cybersmart dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za'
                  }
              }
          }
        },

    # Slots 35-39 are reserved for future South African ISPs
    # Slots 40-49 are now reserved (the old ISP in Iceland that had unmetering
    # is long gone).

    # Regularly installs turn up from Google netblocks; possibly this is part
    # of sandboxed malware scanning of Google Code downloads, but equally for
    # all I know it could be humans, possibly in the Sydney office where they
    # develop Google Maps.

    50: { 'name': 'Google, Inc', 'server': '0.0.0.0',
          'filter': '# What Steam server do Google use...?' },

    # I really have no idea what's going on with installs from Comcast netblocks
    # so I'd hope one day someone using one bothers to explain it to me. I've
    # also seen a few installs from AT&T as well, equally baffling.

    60: { 'name': 'Comcast Communications', 'server': '0.0.0.0',
          'filter': '# No rules for Comcast, please suggest some!' },
    61: { 'name': 'AT&T Internet Services', 'server': '0.0.0.0',
          'filter': '# No rules for AT&T, please suggest some!' }
}
