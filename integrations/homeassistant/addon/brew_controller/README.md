# Brew Controller panel (Home Assistant add-on)

Shows the controller's own web page as a sidebar panel of Home Assistant, through ingress, and
makes it full screen: Home Assistant's sidebar folds into a slide-over drawer (opened by the
small round button at the bottom left), like Music Assistant. Nothing is rebuilt: it is the
controller's page itself, so every function of it works and a firmware update changes it with
no change here.

Because Home Assistant proxies it, the panel also works away from home and in the Home
Assistant app over HTTPS, and Home Assistant's login is in front of the controller's open API.

## Install (Home Assistant OS / Supervised)

1. Copy this folder to `/addons/brew_controller` on the Home Assistant host (the *Advanced SSH &
   Web Terminal* add-on sees it as `/addons`).
2. **Settings > Add-ons > Add-on store**, menu (top right) > **Check for updates**. **Brew Controller**
   appears under **Local add-ons**. Install it and **Start**. "Show in sidebar" is on by default.
3. Set the option **controller** to the controller's address (default `192.168.0.242`). Use an IP
   address: `.local` names are not resolved inside add-ons. Give the controller a fixed address in the
   router, or the panel stops loading when it changes.

## How it works

`nginx` listens on the ingress port, accepts only the Supervisor (`172.30.32.2`), and forwards to
the controller. While passing the page it:

- turns its absolute paths (`/api/...`, `/shots.csv`) into relative ones, so they work under the
  ingress prefix;
- adds `embed.js`, which asks Home Assistant for kiosk mode (`hass-kiosk-mode`) and adds the
  menu button. Opened outside Home Assistant, `embed.js` does nothing.

The controller handles one request at a time per connection, so the proxy closes each upstream
connection. Large uploads (backup restore, firmware file) pass through up to 16 MB.

## Troubleshooting

- Blank page or "502": the address in the option is wrong or the controller is off the network.
  The add-on log shows the address in use.
- Sidebar stays after leaving the panel: reload the page, or press `m` in Home Assistant.
