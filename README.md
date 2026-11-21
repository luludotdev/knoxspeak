# knoxspeak

> make the zomboid voip not suck with the power of TeamSpeak
>
> *sorry teamspeak is so archaic its genuinely the best software for this kind of thing*

## Requirements

- [ZombieBuddy](https://steamcommunity.com/sharedfiles/filedetails/?id=3619862853)
- [TeamSpeak 3 Server](https://www.teamspeak.com/en/downloads?product=server) **(Not TeamSpeak 6)**
- [TeamSpeak 3 Client](https://www.teamspeak.com/en/downloads?product=ts3) **(required by all clients)**
  - The knoxspeak TS3 plugin must be installed and enabled

## Server Setup

### Zomboid

In the server settings `VoiceEnable` must be set to `false` to disable the built in VOIP system.
ZombieBuddy and the knoxspeak Lua plugin must also be loaded for clients to pick it up. No Java is executed on the server.

### TeamSpeak

The TeamSpeak 3 server needs to have an IP that all players can connect to, you probably want to password protect it on closed servers.
Make sure you have at least one user with admin permissions, a privilage key is generated on first boot.

You will probably want more than one channel, a default one that users will connect to, and one that active players should join once their game is loaded.
While on TeamSpeak but not in-game you will hear people normally with no processing, so this can help avoid hearing people you dont want to.

You will probably need to lower the anti-flood thresholds in the server settings. Right-click the server name (top of the channel list) and
click on `Edit Virtual Server` and go to the `Anti-Flood` tab. To be safe set the values to at least 30 / 300 / 3000.
Alternatively you can set the ignore anti flood permission for the default group. *(see below for more info)*

You will also want to make sure idle time is disabled for the default group. Turn on `Advanced permission system` in `Tools -> Options -> Application` then open
`Permissions -> Server Groups` and select the default group. Uncheck `Show granted only`. Make sure `Max Idle time (Seconds)` is greyed out.

## Client Setup

This applies to every client. Everyone who wants to use knoxspeak must have done this setup.

### Zomboid

Ensure ZombieBuddy is active and you are subscribed to knoxspeak on the workshop. If the server has the mods enabled then Zomboid should load it on the client too,
but if you want to be safe you can enable it in the mods menu too.

When enabled, knoxspeak will disable VOIP on your client. You can also manually turn it off in the settings to be safe.

### TeamSpeak

Install the knoxspeak TeamSpeak plugin (shipped with the mod on the workshop, or available for download here on GitHub). Double click the `knoxspeak.ts3_plugin`
**while TeamSpeak is closed**, this will install and activate the plugin. Open TeamSpeak and click on `Self -> Sound Pack -> Sounds deactivated`. This will disable
join / leave sounds etc.

When connected to the server, clicking on your own name should have a line in the right side panel that reads `knoxspeak status:`. If this is missing then the
plugin didnt install correctly, or is disabled. When loading the game the status indicator should turn orange once the Java mod loads and connects to TeamSpeak.
When joining the Zomboid server if all is correct then the status indicator will turn green and knoxspeak will be active.

**When you close Zomboid you will hear everyone normally.** Deafen or move channels to prevent this.
