# If I want to map source IPs to ISP names, you'd normally just do some kind of
# reverse DNS lookup: however, the API for that is blocked by Google App Engine
# and there's no reliable way to bypass it, see
#   http://code.google.com/p/googleappengine/issues/detail?id=354
#
# However, most (paid) GeoIP databases include this data too, although most are
# asking crazy prices. There's even a more-or-less free CSV database for that
# which can be obtained from MaxMind, Inc who sell the full databases, since for
# our purposes the ASN data should be all we need:
#   http://www.maxmind.com/app/asnum
# A suitable Python API for working with this is 
#   http://code.google.com/p/pygeoip/
#
# However, what I've done for simplicity for now is rather than putting the IP
# mappings into the datastore is to prepare a file with a suitable Python data
# literal I can import, such with the subset of IP ranges I care about.

import json
import logging
import string
import ip_match

# Assist the mapping process by converting the IP address string into a number

def stringip_to_number (text):
    fields = string.split (text, '.')
    total = 0
    for item in fields:
        total = (total << 8) + int (item)

    return total;

# Alternate mapping table for IPv6 netblocks; currently there are few of these,
# but Internode in Australia appear to be one such organization. For now I'm
# not bothering to explicitly represent the prefix length, I'm just using a
# simple prefix match.

ipv6_prefixes = {
    "2001:4400:": 0,    # TelstraClear New Zealand
    "2001:4478:": 12,   # iiNet Australia
    "2001:4479:": 12,   # iiNet Australia
    "2001:44B8:": 11,   # Internode Australia
    "2406:E000:": 2	# Snap! New Zealand
};

# Find any matching tuple inside the ip_match.ip_table list, which is sorted,
# and return the ISP number for it
#
# The remapping for loopback below is to help testing; since we don't have a
# real IP to use in the local GAE dev environment, try the various known Steam
# server IPs used by different ISPs to see if we can identify their netblocks.

def find_netblock (ip):
    if ip == '127.0.0.1':
        ip = '203.167.129.4'

    ipType = type (ip)
    if ipType == str or ipType == unicode:
        if ':' in ip:   # look in IPv6 table.
            ip = ip.upper ()
            for prefix in ipv6_prefixes.items ():
                if ip.startswith (prefix [0]):
                    return prefix [1]

            logging.warning ('Unknown mapping for IPv6 address ' + ip)
            return - 1

        ipv4 = stringip_to_number (ip)
    else:
        ipv4 = ip

    # Binary search or linear? Now the table is 1200 long, it's worth it to do
    # a binary search.

    table = ip_match.ip_table
    low = 0
    high = len (table) - 1

    while low <= high:
        mid = (low + high) / 2
        item = table [mid]

        if item [0] >= ipv4:
            # Move down
            high = mid - 1
            continue

        if item [1] >= ipv4:
            # We have a match
            return item [2]

        # Move up
        low = mid + 1

    if type (ip) == str:
        logging.warning ('Unknown mapping for IPv4 address ' + ip)

    return - 1

# Handle notifying the project owner when a feedback form or uploaded
# submission occurs. For now this is direct, but in future this could
# equally well be done using the cron job API to roll up notifications in
# a batch.
#
# Note that the free tier of GAE severely restricts the amoun of outbound
# e-mail to 100 messages/day, hence why XMPP notification is a better kind
# of default if significant traffic is expected. Not that I do expect any
# more than one or two actual notifications to occur, but it's nice to show

def notifyOwner (text, kind):
    # Send an invitation first so that the GAE instance gets permission
    # as a contact so that later messages are received properly instead of
    # getting binned (as the GTalk servers tend to do, as a spam-control
    # measure).
    #
    # This is particularly useful when running a test instance of a GAE app
    # as each will use a distinct source JID for itself.

    who = 'nigel.bree@gmail.com'
    xmpp.send_invite (who)
    status = xmpp.send_message (who, 'Posted ' + kind + ': ' + text)

    if status == xmpp.NO_ERROR:
        return

    # If XMPP is unavailable, fall back to e-mail

    mail.send_mail ('Feedback <feedback@steam-limiter.appspotmail.com>',
                    'Nigel Bree <nigel.bree@gmail.com>',
                    'New ' + kind + ' posted',
                    text)

# Since we're sending back data rather than plain text, abstract out the
# wrapping of the data, permitting the caller to ask for JSONP style.

def send (handler, data):
    cb = handler.request.get ('cb', '()')
    if cb == '()':
        handler.response.out.write (data)
        return

    # This header already exists in the collection so adjust rather than add
    handler.response.headers ['Content-Type'] = 'application/json; charset=utf-8'

    if cb != '':
        handler.response.out.write (cb + '(')
    handler.response.out.write (json.dumps (data))
    if cb != '':
        handler.response.out.write (')')

# All the data we care about, all in a dict, for various handlers to choose
# from to render

def bundle (self, isps, defaults):
    source = self.request.get ('ip', self.request.remote_addr)
    netblock = find_netblock (source)

    # GAE actually includes a small amount of GeoIP itself; not what need for
    # ISP selection, but interesting nonetheless (note: only in production,
    # not in the dev server)
    # http://code.google.com/appengine/docs/python/runtime.html#Request_Headers

    country = self.request.headers.get ('X-AppEngine-Country')
    country = country or 'Unknown'

    isp = isps.get (netblock)
    proxy = isp.get ('proxy')

    result = defaults.copy ()

    # Allow a simpler rule format by naming a proxy server, which gets
    # the filter and allow rules expanded from a template. For now I'm
    # doing this all server-side rather than exposing it to the client
    # script, to keep UI compatibility

    if 'proxyfilter' in result:
        proxyfilter = result ['proxyfilter']
        del result ['proxyfilter']
    else:
        proxyfilter = ''
    if 'proxyfilter' in isp:
        proxyfilter = isp ['proxyfilter']

    if 'proxyallow' in result:
        proxyallow = result ['proxyallow']
        del result ['proxyallow']
    else:
        proxyallow = ''
    if 'proxyallow' in isp:
        proxyallow = isp ['proxyallow']

    result ['ispname'] = isp ['name']
    result ['country'] = country

    if proxy and proxyfilter and proxyallow:
        result ['proxy'] = proxy
        result ['filterrule'] = proxyfilter % { 'proxy': proxy }
        result ['allow'] = proxyallow % { 'proxy': proxy }
    else:
        result ['filterrule'] = isp.get ('filter') or isp ['server']
        result ['allow'] = isp.get ('allow') or ''

    test = isp.get ('test')
    if test:
        result ['test'] = test

    return result
