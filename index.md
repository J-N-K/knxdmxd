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
      { "name" : "test", "dmx" : "2.450", "statusga" : "14/1/3" }
    ]

### &quot;name&quot; : &lt;string&gt;

Channel names are not restricted in length or composition. They must be unique within the configuration. It is recommended not to use umlauts.

### &quot;dmx&quot; : &lt;string&gt;

The DMX address has the notation &lt;universe&gt;.&lt;channel&gt;. According to E1.31 standard numbering, channel 0 and universe 0 do not exists. If the universe part is ommitted, universe 1 is assumed.

### &quot;statusga&quot; : &lt;string&gt;

If defined, a telegram (DPT5) with the current channel value will be send via EIBD to the specified address whenever a crossfade finishes.

### &quot;factor&quot; : &lt;float&gt;

Defaults to 1.0. All values set within knxdmxd for this channel are multiplied with this value. Can be used to adjust colors in RGB setups or for brightness corrections in multi-unit setups.

## dimmers

Dimmers allow changing the value of single channels. Example:

    "dimmers": [  
      { "name" : "test", "channel" : "test", "ga" : "14/0/3", "fading" : 0 }
    ]

### &quot;name&quot; : &lt;string&gt;

Dimmer names are not restricted in length or composition. They must be unique within the configuration. It is recommended not to use umlauts.

### &quot;channel&quot; : &lt;string&gt;

Must contain a valid channel name defined in the channels section.

### &quot;ga&quot; : &lt;string&gt;

Group address that this dimmer should respond to. The channel will be set to value (DPT5) send to this address.

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







