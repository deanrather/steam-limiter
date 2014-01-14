#!/usr/bin/env python
#
# Copyright (C) 2011 Nigel Bree
# All Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

#-----------------------------------------------------------------------------

# This application mainly provides a simple way for installations of the Steam
# Limiter application from http://steam-limiter.googlecode.com to determine
# whether an updated version of the application has been made available. That's
# just a simple matter of retrieving some data from a URL to retrieve the
# current version number and a download URL for the latest installer.
#
# Given the way that web access to the source repository in Google Code works
# in principle that could be used instead, but there are a couple of advantages
# to having a service like this instead, in addition to the fact that as with
# the NSIS installer for the limiter client it's a handy example of how to do
# such things.
#
# For instance, an additional thing I could add to this is to have an installer
# extension for the limiter client app which can retrieve the client's real IP
# and thus suggest to it the ideal server (or server list) to set as the filter
# default, instead of assuming TelstraClear - TC is the default ISP at present
# since that's my ISP, but the filter app is usable by other New Zealand ISPs
# and it would be nice to be able to make that seamless.
#

import jinja2
import os
import webapp2
import logging

from google.appengine.ext.webapp import template
from google.appengine.ext import db
from google.appengine.api import users, xmpp, mail

# These will most likely eventually become some datastore items in future, but
# making them static will do just to start the update support off.

self_base = 'http://steam-limiter.appspot.com'
if 'Development' in os.environ ['SERVER_SOFTWARE']:
    self_base = 'http://localhost:8080'

old_defaults = {
    'latest': '0.7.0.3',
    'download': 'http://steam-limiter.googlecode.com/files/steamlimit-0.7.0.3.exe'
}

new_defaults = {
    'latest': '0.7.1.0',
    'download': self_base + '/files/steamlimit-0.7.1.0.exe',
    'proxyfilter': 'content*.steampowered.com=%(proxy)s;*.cs.steampowered.com=%(proxy)s',
    'proxyallow': '//%(proxy)s=*;//content*.steampowered.com=%(proxy)s;//cs.steampowered.com=%(proxy)s'
}

import app_common
import old_isps

# Data on unmetering for ISPs, for the expanded rule types available in the
# v2 of the configuration API, for steam-limiter 0.7.1.0 and later where I
# can now handle rewriting "CS" type servers to normal ones, and where I can
# thus afford to use a simpler rule format for ISPs running proxy servers to
# do unmetering (which is most of them).
#
# For this version, I'm also completely dropping all the old Port 27030 rules

new_isps = {
    - 1: { 'name': 'Unknown', 'server': '0.0.0.0',
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

    0: { 'name': 'TelstraClear New Zealand',
         'proxy': 'steam.cdn.vodafone.co.nz' },

    10: { 'name': 'Telstra BigPond Australia',
          'proxy': 'steam.content.bigpondgames.com' },

    11: { 'name': 'Internode Australia',
          'proxy': 'steam.cdn.on.net' },

    12: { 'name': 'iiNet Australia',
          'proxy': 'steam.cdn.on.net' },

    # iPrimus evidently support a small list of Steam servers hosted in some
    # regional peering exchanges, and that's it (no proxy servers).
    
    14: { 'name': 'iPrimus Australia',
          'filter': 'content*.steampowered.com=valve217.cs.steampowered.com;' +
                    '*.cs.steampowered.com=valve217.cs.steampowered.com',
          'allow': '//*.steampowered.com=*' },

    # Similarly to iPrimus, these small regional ISPs don't document what they
    # do and some of this data may be out of data due to acquisitions, since the
    # iiNet group has acquired a lot of regional ISPs.

    15: { 'name': 'Westnet Internet Services (Perth, WA)',
          'filter': 'content*.steampowered.com=valve217.cs.steampowered.com,files-oc-syd.games.on.net',
          'allow': '//steam.cdn.on.net=*' },

    16: { 'name': 'Adam Internet (Adelaide, SA)',
          'filter': '*:27030=valve217.cs.steampowered.com,files-oc-syd.games.on.net;' +
                    'content*.steampowered.com=valve217.cs.steampowered.com,files-oc-syd.games.on.net',
          'allow': '//steam.cdn.on.net=*' },

    17: { 'name': 'EAccess Broadband, Australia',
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
          'allow': '//steam.isgaming.co.za=*',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/IS dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za',
                      'allow': '//*.wa.co.za=*;//content*.steampowered.com=*'
                  }
              }
          }
        },
    31: { 'name': 'webafrica (Cape Town, South Africa)', 'server': '41.185.24.21',
          'filter': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za',
           'allow': '//*.wa.co.za=*;//content*.steampowered.com=*'
        },
    32: { 'name': 'Telkom SAIX, South Africa', 'server': '0.0.0.0',
          'filter': '# No known unmetered Steam server',
          'test': {
              'report': True,
              'steam.wa.co.za icmp *.wa.co.za': {
                  0: {
                      'ispname': 'WebAfrica/SAIX dual ISP',
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za',
                      'allow': '//*.wa.co.za=*;//content*.steampowered.com=*'
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
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za',
                      'allow': '//*.wa.co.za=*;//content*.steampowered.com=*'
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
                      'filterrule': '*:27030=steam.wa.co.za,steam2.wa.co.za;content*.steampowered.com=steam.wa.co.za,steam2.wa.co.za',
                      'allow': '//*.wa.co.za=*;//content*.steampowered.com=*'
                  }
              }
          }
        },

    # Slots 35-39 are reserved for future South African ISPs
    # Slots 40-49 are reserved for future use (used to be for Iceland but that
    # country is deprecated as it has no unmetered servers).

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

# Simple utility cliches.

def bundle (handler, isps = new_isps, defaults = new_defaults,
            source = None):
    return app_common.bundle (handler, isps, defaults, source)

def send (handler, data = None, key = None):
    isps = new_isps
    defaults = new_defaults
    
    ver = handler.request.get ('v', default_value = None)
    if ver is None or ver == '0':
       agent = handler.request.headers ['User-Agent']
       if ver == '0' or agent.startswith ('steam-limiter/'):
           isps = old_isps.isps
           defaults = old_defaults

    alt_addr = handler.request.get ('ip', default_value = None)
    if not data:
        data = bundle (handler, isps, defaults, alt_addr)

    if key:
        data = data.get (key)
    app_common.send (handler, data)

def expand (handler, name, context):
    path = os.path.join (os.path.dirname (__file__), name)
    handler.response.out.write (template.render (path, context))

# The landing page for human readers to see

class MainHandler (webapp2.RequestHandler):
    def get (self):
        context = {
            'user': users.get_current_user ()
        }
        expand (self, 'index.html', context)

# The query page for the latest revision, which can information about the latest
# version number in various forms

class LatestHandler (webapp2.RequestHandler):
    def get (self):
        send (self, key = 'latest')

# A query page for the path to the latest download; I used to have an option
# to redirect to the download, but that's now available via /get
#
# This is one of the places versions matter, since older versions won't
# auto-upgrade if we can't point at Google Code. It's likely that I'll have
# two download systems, one for older pre-0.7.1.0 versions and one for the
# newer ones that allows download from this service.

class DownloadHandler (webapp2.RequestHandler):
    def get (self):
        send (self, key = 'download')

# A query page for exercising the IP->ISP mapping; the bit below for loopback
# is for local testing since that doesn't yield a valid IP for the matching
# algorithm to use.

class IspHandler (webapp2.RequestHandler):
    def get (self):
        send (self, key = 'ispname')

# Return the newer style of filter list.

class FilterRuleHandler (webapp2.RequestHandler):
    def get (self):
        send (self, key = 'filterrule')

# Return a customized server list, or the default global one

class AllowHostHandler (webapp2.RequestHandler):
    def get (self):
        send (self, key = 'allow')

# Return a bundle of all the configuration pieces as a JSON-style
# map.

class BundleHandler (webapp2.RequestHandler):
    def get (self):
        send (self)

# Feedback model for the feedback submission form to persist

class Feedback (db.Model):
    content = db.TextProperty ()
    source = db.StringProperty ()
    timestamp = db.DateTimeProperty (auto_now = True)

# Handle a feedback form, to allow people to spam me with whatever they like...
# given that currently I'm suffering from a lack of feedback, this is meant
# to help overcome that. We shall see if it works.

class FeedbackHandler (webapp2.RequestHandler):
    def get (self):
        expand (self, 'feedback.html', { })

    def post (self):
        text = self.request.get ('content')

        if text != '':
            item = Feedback (content = text, source = self.request.remote_addr)
            item.put ()

            notifyOwner (text, 'feedback')

        expand (self, 'thanks.html', { })

# Similar to the general text feedback, we can have users upload their custom
# rules as suggestions for future versions or revisions of the rule base now
# that the rulebase exists completely in the webservice.

class UploadedRule (db.Model):
    ispName = db.StringProperty ()
    filterRule = db.StringProperty (multiline = True)
    notes = db.StringProperty (multiline = True)
    source = db.StringProperty ()
    country = db.StringProperty ()
    timestamp = db.DateTimeProperty (auto_now = True)

# Handle a new-rule suggestion form, intended to support a future automatic
# upload of a user's custom rules.

class UploadRuleHandler (webapp2.RequestHandler):
    def get (self):
        expand (self, 'uploadrule.html', { })

    def post (self):
        isp = self.request.get ('ispname')
        rule = self.request.get ('filterrule')
        note = self.request.get ('content')

        country = self.request.headers.get ('X-AppEngine-Country')
        country = country or 'Unknown'

        if rule != '':
            item = UploadedRule (ispName = isp, filterRule = rule, notes = note,
                                 source = self.request.remote_addr,
                                 country = country)
            item.put ()

            notifyOwner (isp + ' ==> ' + rule + '\n' + note, 'rule')

        expand (self, 'thanks.html', { })

# Handle a posted report from a special local test - this is primarily used
# in beta builds to see how some of the client-end rule extensions are being
# processed.

class TestReportHandler (webapp2.RequestHandler):
    def get (self):
        expand (self, 'uploadrule.html', { })

    def post (self):
        test = self.request.get ('test')
        result = self.request.get ('result')

        country = self.request.headers.get ('X-AppEngine-Country')
        country = country or 'Unknown'

        notifyOwner (test + ' ==> ' + result + '\n', 'test')
        expand (self, 'thanks.html', { })

# Custom 404 that suggests filing an issue rather than the default blank.

class NotFoundHandler (webapp2.RequestHandler):
    def get (self):
        self.error (404)
        expand (self, 'default_error.html', { })

# Plumb up the GAE boilerplate with a mapping of URLs to handlers.

app = webapp2.WSGIApplication ([('/', MainHandler),
                                ('/latest', LatestHandler),
                                ('/download', DownloadHandler),
                                ('/ispname', IspHandler),
                                ('/filterrule', FilterRuleHandler),
                                ('/allow', AllowHostHandler),
                                ('/all', BundleHandler),
                                ('/feedback', FeedbackHandler),
                                ('/uploadrule', UploadRuleHandler),
                                ('/testreport', TestReportHandler),
                                ('/.*', NotFoundHandler)],
                               debug = True)

def main ():
    application.run ()

if __name__ == '__main__':
    main ()
