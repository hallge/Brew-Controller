#!/bin/sh
# Renders the nginx config from the add-on option and runs nginx in the foreground.
set -eu
HOST="$(jq -r '.controller // empty' /data/options.json | sed 's#^https\?://##; s#/*$##')"
if [ -z "$HOST" ]; then
  echo "The option 'controller' is empty: set the controller's address (for example 192.168.0.242)." >&2
  exit 1
fi
echo "Brew Controller panel: proxying to http://$HOST/"
sed "s#__CONTROLLER__#$HOST#g" /etc/nginx/brew.conf.tpl > /etc/nginx/nginx.conf
exec nginx -g 'daemon off;'
