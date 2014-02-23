---
layout: default
---

# What is knxdmxd

knxdmxd is a daemon that connects building automation using KNX (via EIBD) to DMX lighting equipment using E1.31.

# Command line options

## -u &lt;address&gt;

Address of EIBD, defaults to local:/tmp/eib. You can use EIBD-IP connections with ip:&lt;IP-address&gt;.

## -c &lt;filename&gt;

Path and filename of configuration file. It is recommended to locate the knxdmxd.conf in /etc/knxdmxd.conf. If no option is given, the current directory is used (i.e. ./knxdmxd.conf).

## -p &lt;filename&gt;

Path and filename of PID file. It is recommended to locate the knxdmxd.pid in /var/run/knxdmxd.pid. If no option is given, the current directory is used (i.e. ./knxdmxd.pid).

## -d

Run as daemon.

# Configuration

the configuration file conforms to JSON-standard. Malformed configuration files may cause knxdmxd to crash. Strings have to be encapuslated with quotation marks.

## channels

Channels are the basic building block of the configuration. They consist of at least have a name and DMX-address. Example:

    "channels" : [
      { "name" : "test", "dmx" : "2.450", "statusga" : {"switch" : "14/6/3", "value" : "14/7/3" }
    ]

### &quot;name&quot; : &lt;string&gt;

Channel names are not restricted in length or composition. They must be unique within the configuration. It is recommended not to use umlauts.

### &quot;dmx&quot; : &lt;string&gt;

The DMX address has the notation &lt;universe&gt;.&lt;channel&gt;. According to E1.31 standard numbering, channel 0 and universe 0 do not exists. If the universe part is ommitted, universe 1 is assumed.

### &quot;statusga&quot; : &lt;object&gt;

This object is optional. It can consist of two members &quot;switchquot; and &quot;value&quot;. The old-style status-object (a single string for the value-address) is still supported but deprecated and my be removed in future versions. 

#### &quot;switch&quot; : &lt;string&gt;

If defined, a telegram (DPT1) will be send via EIBD to the specified address whenever a crossfade finishes. The value is 0/false/off if the channel value is 0 and 1/true/on if the channel value is above 0.

#### &quot;value&quot; : &lt;string&gt;

If defined, a telegram (DPT5) will be send via EIBD to the specified address whenever a crossfade finishes. The value is the current value of the channel.

### &quot;factor&quot; : &lt;float&gt;

Defaults to 1.0. All values set within knxdmxd for this channel are multiplied with this value. Can be used to adjust colors in RGB setups or for brightness corrections in multi-unit setups.

## dimmers

Dimmers allow changing the value of single channels. Example:

    "dimmers": [  
      { "name" : "test", "channel" : "test", "ga" : { "switch: "14/0/1", "value": "14/1/1" }, "turnonvalue" : 255, "fading" : 0 }
    ]

### &quot;name&quot; : &lt;string&gt;

Dimmer names are not restricted in length or composition. They must be unique within the configuration. It is recommended not to use umlauts.

### &quot;channel&quot; : &lt;string&gt;

Must contain a valid channel name defined in the channels section.

### &quot;ga&quot; : &lt;object&gt;

Group address(es) that this dimmer should respond to. This object can consist of two objects: &quot;switch&quot; and &quot;value&quot; The old-style format (string instead of object for value-only-address) is deprecated and may be removed in future versions. The object

#### &quot;switch&quot; : &lt;string&gt;

Group address that switches this dimmer on (1) or off (0). The telegram has to be DPT1. If a &quot;turnonvalue&quot;-value is defined, the dimmer ist set to that value, if the configured value is 0 or missing, the last value before turn-off is used.

#### &quot;value&quot; : &lt;string&gt;

Group address that changes the value of this dimmer. The telegram has to be DPT5.

### &quot;turnonvalue&quot; : &lt;integer&gt;

This value is optional and used as the value that the dimmer ist set to, when it is switched on by the &quot;switch&quot;-address. If the value is missing or set to 0, the last-value before turn-off is used.

### &quot;fading&quot; : &lt;float&gt;

Time in seconds that is used to fade between to values. A value of 0.0 disables fading (instant change).

## scenes

Scenes change multiple channel values to predefined values. Example:

    "scenes": [ 
      { "name" : "Bad_an",
        "trigger" : {
          "go" : {"knx" : "1/0/130", "value" : 1 }
        },
        "data" : [
          { "channel" : "bad_r", "value" : 255 },
          { "channel" : "bad_g", "value" : 255 },
          { "channel" : "bad_b", "value" : 255 }
        ],
        "fading" : {  
          "in" : 1.0,
          "out": 1.0,
        }
      }
    ]


### &quot;name&quot; : &lt;string&gt;

Scene names are not restricted in length or composition. They must be unique within the configuration. It is recommended not to use umlauts.

### &quot;trigger&quot; : &lt;object&gt;

(see trigger section below)

### &quot;data&quot; : &lt;array&gt;

The data section contains an array of objects, each describing one channel. Each channel object consists of two values, the name of the channel (as defined in the channels section above) and a corresponding value for this scene. 

### &quot;fading&quot; : &lt;object&gt;

The fading object is optional. If no fading is defined, A value of 0.0 is assumed. Possible childs of the fading object are

#### &quot;in&quot; : &lt;float&gt;

Time in seconds that is used, if the previous value of the channel is below the value defined for this scene.

#### &quot;out&quot; : &lt;float&gt;

Time in seconds that is used, if the previous value of the channel is above the value defined for this scene.

#### &quot;time&quot; : &lt;float&gt;

Time in seconds that is used for all value changes. This setting has priority over &quot;in&quot; and &quot;out&quot;.







