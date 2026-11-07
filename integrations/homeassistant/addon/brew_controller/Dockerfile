FROM nginx:1.27-alpine
RUN apk add --no-cache jq
COPY run.sh /run.sh
COPY nginx.conf.tpl /etc/nginx/brew.conf.tpl
COPY embed.js /etc/nginx/embed.js
RUN chmod +x /run.sh
CMD ["/run.sh"]
