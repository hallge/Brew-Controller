worker_processes 1;
error_log /dev/stderr warn;
pid /tmp/nginx.pid;

events { worker_connections 64; }

http {
  access_log off;
  include /etc/nginx/mime.types;
  default_type application/octet-stream;
  client_body_temp_path /tmp/nginx_body;
  proxy_temp_path /tmp/nginx_proxy;
  fastcgi_temp_path /tmp/nginx_fcgi;
  uwsgi_temp_path /tmp/nginx_uwsgi;
  scgi_temp_path /tmp/nginx_scgi;

  server {
    listen 8099;

    # Only Home Assistant's Supervisor (ingress) may talk to this add-on.
    allow 172.30.32.2;
    deny all;

    # Backups and firmware files pass through.
    client_max_body_size 16m;

    # The script that makes the page full screen inside Home Assistant.
    location = /_embed.js {
      alias /etc/nginx/embed.js;
      default_type application/javascript;
      add_header Cache-Control "no-store";
    }

    location / {
      proxy_pass http://__CONTROLLER__;
      proxy_http_version 1.1;
      # The controller is a small chip with few sockets: one request per connection.
      proxy_set_header Connection close;
      proxy_set_header Host $proxy_host;
      # Plain text, so the page can be rewritten.
      proxy_set_header Accept-Encoding "";
      proxy_request_buffering off;
      proxy_read_timeout 300s;
      proxy_send_timeout 300s;

      # The page was written for the site root; under ingress its absolute
      # paths must become relative ones.
      sub_filter_once off;
      sub_filter_types application/javascript;
      sub_filter '<head>' '<head><script src="./_embed.js"></script>';
      sub_filter 'href="/' 'href="./';
      sub_filter "href='/" "href='./";
      sub_filter '"/api/' '"./api/';
      sub_filter "'/api/" "'./api/";
      sub_filter '`/api/' '`./api/';
    }
  }
}
