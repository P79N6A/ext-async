FROM alpine:3.8

ENV PHP_VERSION nightly
RUN apk add --no-cache build-base git curl libressl-dev libxml2-dev autoconf bison bash automake libtool

# Install PHP:
RUN mkdir /usr/local/php

WORKDIR /usr/local/php

RUN mkdir cli php-src
RUN curl -LSs https://github.com/php/php-src/archive/7.3.0.tar.gz | tar -xz -C "php-src" --strip-components 1

WORKDIR /usr/local/php/php-src

RUN ./buildconf --force
RUN ./configure \
    --prefix=/usr/local/php/cli \
    --with-config-file-path=/usr/local/php/cli \
    --with-zlib \
    --without-pear \
    --enable-debug \
    --enable-pcntl \
    --enable-sockets

RUN make -j4 && make install

RUN touch /usr/local/php/cli/php.ini \
    && chmod 466 /usr/local/php/cli/php.ini \
    && ln -s /usr/local/php/cli/bin/php /usr/local/bin/php \
    && ln -s /usr/local/php/cli/bin/phpize /usr/local/bin/phpize \
    && ln -s /usr/local/php/cli/bin/php-config /usr/local/bin/php-config

COPY . /tmp/ext-async

WORKDIR /tmp/ext-async

RUN phpize --clean \
    && phpize

RUN ./configure
RUN make install -B
RUN echo "extension=\"async.so\"" >> /usr/local/php/cli/php.ini

RUN make test-coverage
RUN php -v
RUN php -m
