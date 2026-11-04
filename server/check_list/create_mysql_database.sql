/*
SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
SPDX-License-Identifier: MIT
*/

create table stackChan.app_store
(
    id           bigint auto_increment
        primary key,
    app_name     varchar(128)                         not null comment 'App name',
    app_icon_url varchar(512)                         null comment 'App icon URL',
    description  text                                 null comment 'App description',
    firmware_url varchar(512)                         null comment 'Firmware/installer download URL',
    create_at    datetime   default CURRENT_TIMESTAMP null comment 'Created at',
    update_at    datetime   default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP comment 'Updated at',
    is_deleted   tinyint(1) default 0                 not null comment 'Deleted flag, 0 = normal, 1 = deleted'
)
    comment 'App Store application list';

create table stackChan.device_dance
(
    id         bigint auto_increment
        primary key,
    mac        varchar(17)                        not null comment 'Device MAC address',
    dance_name varchar(64)                        null comment 'Dance name',
    dance_data json                               not null comment 'MotionData',
    music_url  varchar(255)                       null comment 'Dance background music URL',
    created_at datetime default CURRENT_TIMESTAMP null,
    updated_at datetime default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP
);

create table stackChan.device_friend
(
    mac_a varchar(17) not null,
    mac_b varchar(17) not null,
    primary key (mac_a, mac_b)
);

create index fk_friend_mac_b
    on stackChan.device_friend (mac_b);

create table stackChan.user
(
    uid             bigint                                not null comment 'Unique user UID (remote platform primary key)'
        primary key,
    username        varchar(64)                           not null comment 'Login username',
    userslug        varchar(64)                           null comment 'User slug',
    display_name    varchar(64)                           null comment 'Display name',
    icon_text       char                                  null comment 'User icon text',
    icon_bg_color   varchar(16)                           null comment 'Icon background color',
    email_confirmed tinyint(1)  default 0                 null comment 'Email verified, 0 = no, 1 = yes',
    join_date       bigint                                null comment 'Registration timestamp (ms)',
    last_online     bigint                                null comment 'Last online timestamp (ms)',
    user_status     varchar(32) default 'online'          null comment 'User online status',
    create_at       datetime    default CURRENT_TIMESTAMP null comment 'Local created at',
    update_at       datetime    default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP comment 'Local updated at',
    is_deleted      tinyint(1)  default 0                 not null comment 'Deleted flag, 0 = normal, 1 = deleted',
    constraint idx_username
        unique (username)
)
    comment 'User information';

create table stackChan.device
(
    mac       varchar(17)  not null
        primary key,
    name      varchar(255) null,
    uid       bigint       null comment 'Bound user UID',
    bind_time varchar(32)  null comment 'Device bind time',
    constraint fk_device_user_uid
        foreign key (uid) references stackChan.user (uid)
            on update cascade on delete set null
);

create index idx_device_uid
    on stackChan.device (uid);

create table stackChan.device_pano
(
    id         bigint auto_increment
        primary key,
    mac        varchar(17)                        not null comment 'Device MAC address',
    pano_url   varchar(512)                       not null comment 'Panorama image URL',
    created_at datetime default CURRENT_TIMESTAMP null comment 'Created at',
    updated_at datetime default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP,
    constraint fk_pano_mac
        foreign key (mac) references stackChan.device (mac)
            on delete cascade
);

create index idx_device_pano_mac
    on stackChan.device_pano (mac);

create table stackChan.device_post
(
    id            bigint auto_increment
        primary key,
    mac           varchar(17)                        not null comment 'Posting device MAC',
    content_text  text                               null,
    content_image varchar(512)                       null comment 'Image URL',
    created_at    datetime default CURRENT_TIMESTAMP null comment 'Posted at',
    constraint fk_post_mac
        foreign key (mac) references stackChan.device (mac)
);

create table stackChan.device_post_comment
(
    id         bigint auto_increment
        primary key,
    post_id    bigint                             not null comment 'Post ID',
    mac        varchar(17)                        not null comment 'Commenting device MAC',
    content    text                               null,
    created_at datetime default CURRENT_TIMESTAMP null comment 'Commented at',
    constraint fk_comment_mac
        foreign key (mac) references stackChan.device (mac),
    constraint fk_comment_post
        foreign key (post_id) references stackChan.device_post (id)
            on delete cascade
);
