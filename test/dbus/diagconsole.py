# diagconsole
#
# Copyright (C) 2012 Intel Corporation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU Lesser General Public License,
# version 2.1, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
# for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#
# Mark Ryan <mark.d.ryan@intel.com>
#

import dbus
import sys
import json

def print_properties(props):
    print json.dumps(props, indent=4, sort_keys=True)

class Device(object):

    def __init__(self, path):
        bus = dbus.SessionBus()
        self._propsIF = dbus.Interface(bus.get_object(
                'com.intel.dleyna-diagnostics', path),
                                         'org.freedesktop.DBus.Properties')
        self._deviceIF = dbus.Interface(bus.get_object(
                'com.intel.dleyna-diagnostics', path),
                                         'com.intel.dLeynaDiagnostics.Device')

    def get_props(self, iface = ""):
        return self._propsIF.GetAll(iface)

    def get_prop(self, prop_name, iface = ""):
        return self._propsIF.Get(iface, prop_name)

    def print_prop(self, prop_name, iface = ""):
        print_properties(self._propsIF.Get(iface, prop_name))

    def print_props(self, iface = ""):
        print_properties(self._propsIF.GetAll(iface))

    def cancel(self):
        return self._deviceIF.Cancel()

    def print_icon(self, mime_type, resolution):
        bytes, mime = self._deviceIF.GetIcon(mime_type, resolution)
        print "Icon mime type: " + mime

    def get_test_info(self, test_id):
        return self._deviceIF.GetTestInfo(test_id)

    def cancel_test(self, test_id):
        return self._deviceIF.CancelTest(test_id)

    def ping(self, host, repeat_count = 0, interval = 0, data_block_size = 0,
             dscp = 0):
        return self._deviceIF.Ping(host, repeat_count, interval,
                                   data_block_size, dscp)

    def get_ping_result(self, test_id):
        return self._deviceIF.GetPingResult(test_id)

    def nslookup(self, hostname, dns_server = "", repeat_count = 0,
                 interval = 0):
        return self._deviceIF.NSLookup(hostname, dns_server, repeat_count,
                                       interval)

    def get_nslookup_result(self, test_id):
        return self._deviceIF.GetNSLookupResult(test_id)

    def traceroute(self, host, timeout = 0, data_block_size = 0,
                   max_hop_count = 0, dscp = 0):
        return self._deviceIF.Traceroute(host, timeout, data_block_size,
                                         max_hop_count, dscp)

    def get_traceroute_result(self, test_id):
        return self._deviceIF.GetTracerouteResult(test_id)

    def dump_tests(self):
        for i in self.get_prop("TestIDs"):
            test_type, test_state = self.get_test_info(i)
            print u"--- Test ID#" + str(i)
            print u" Type = " + test_type
            print u" State = " + test_state
            if test_state == "Completed":
                if test_type == "Ping":
                    result = json.dumps(self.get_ping_result(i), 4, True)
                elif test_type == "NSLookup":
                    result = json.dumps(self.get_nslookup_result(i), 4, True)
                elif test_type == "Traceroute":
                    result = json.dumps(self.get_traceroute_result(i), 4, True)
                print u" Result = " + result

class UPNP(object):

    def __init__(self):
        bus = dbus.SessionBus()
        self._manager = dbus.Interface(bus.get_object(
                                                'com.intel.dleyna-diagnostics',
                                                '/com/intel/dLeynaDiagnostics'),
                                       'com.intel.dLeynaDiagnostics.Manager')
        self._propsIF =  dbus.Interface(bus.get_object(
                                                'com.intel.dleyna-diagnostics',
                                                '/com/intel/dLeynaDiagnostics'),
                                        'org.freedesktop.DBus.Properties')

    def device_from_name(self, friendly_name):
        retval = None
        for i in self._manager.GetDevices():
            device = Device(i)
            device_name = device.get_prop("FriendlyName").lower()
            if device_name.find(friendly_name.lower()) != -1:
                retval = device
                break
        return retval

    def device_from_udn(self, udn):
        retval = None
        for i in self._manager.GetDevices():
            device = Device(i)
            if device.get_prop("UDN") == udn:
                retval = device
                break
        return retval

    def devices(self):
        for i in self._manager.GetDevices():
            try:
                device = Device(i)
                try:
                    devName = device.get_prop("FriendlyName");
                except Exception:
                    devName = device.get_prop("DisplayName");
                print u'{0:<30}{1:<30}'.format(devName , i)
            except dbus.exceptions.DBusException, err:
                print u"Cannot retrieve properties for " + i
                print str(err).strip()[:-1]

    def version(self):
        print self._manager.GetVersion()

    def rescan(self):
        self._manager.Rescan()

    def white_list_enable(self, enable):
        self.set_prop("WhiteListEnabled", enable)

    def white_list_add(self, entries):
        white_list = set(self.get_prop('WhiteListEntries'))
        white_list = (white_list | set(entries)) - set('')
        self.set_prop("WhiteListEntries", list(white_list))

    def white_list_remove(self, entries):
        white_list = set(self.get_prop('WhiteListEntries'))
        white_list = white_list - set(entries)
        self.set_prop("WhiteListEntries", list(white_list))

    def white_list_clear(self):
        self.set_prop("WhiteListEntries", [''])

    def get_props(self, iface = ""):
        return self._propsIF.GetAll(iface)

    def get_prop(self, prop_name, iface = ""):
        return self._propsIF.Get(iface, prop_name)

    def set_prop(self, prop_name, val, iface = ""):
        return self._propsIF.Set(iface, prop_name, val)

    def print_prop(self, prop_name, iface = ""):
        print_json(self._propsIF.Get(iface, prop_name))

    def print_props(self, iface = ""):
        print_json(self._propsIF.GetAll(iface))
