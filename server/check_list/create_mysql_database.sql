create table stackChan.app_store
(
    id           bigint auto_increment
        primary key,
    app_name     varchar(128)                         not null comment 'App 名称',
    app_icon_url varchar(512)                         null comment 'App 图标 URL',
    description  text                                 null comment 'App 描述信息',
    firmware_url varchar(512)                         null comment '固件 / 安装包下载地址',
    create_at    datetime   default CURRENT_TIMESTAMP null comment '创建时间',
    update_at    datetime   default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP comment '更新时间',
    is_deleted   tinyint(1) default 0                 not null comment '是否删除，0 正常 1 删除'
)
    comment 'App Store 应用列表表';

create table stackChan.device_dance
(
    id         bigint auto_increment
        primary key,
    mac        varchar(17)                        not null comment '设备MAC地址',
    dance_name varchar(64)                        null comment '舞蹈名称',
    dance_data json                               not null comment 'MotionData',
    music_url  varchar(255)                       null comment '舞蹈背景音乐URL',
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
    uid             bigint                                not null comment '用户唯一UID（远程平台主键）'
        primary key,
    username        varchar(64)                           not null comment '登录用户名',
    userslug        varchar(64)                           null comment '用户别名',
    display_name    varchar(64)                           null comment '用户显示名称',
    icon_text       char                                  null comment '用户图标文字',
    icon_bg_color   varchar(16)                           null comment '图标背景色',
    email_confirmed tinyint(1)  default 0                 null comment '邮箱是否验证 0-否 1-是',
    join_date       bigint                                null comment '注册时间戳(毫秒)',
    last_online     bigint                                null comment '最后在线时间戳(毫秒)',
    user_status     varchar(32) default 'online'          null comment '用户在线状态',
    create_at       datetime    default CURRENT_TIMESTAMP null comment '本地创建时间',
    update_at       datetime    default CURRENT_TIMESTAMP null on update CURRENT_TIMESTAMP comment '本地更新时间',
    is_deleted      tinyint(1)  default 0                 not null comment '是否删除 0-正常 1-删除',
    constraint idx_username
        unique (username)
)
    comment '用户信息表';

create table stackChan.device
(
    mac       varchar(17)  not null
        primary key,
    name      varchar(255) null,
    uid       bigint       null comment '绑定的用户UID',
    bind_time varchar(32)  null comment '设备绑定时间',
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
    mac        varchar(17)                        not null comment '设备MAC地址',
    pano_url   varchar(512)                       not null comment '全景图URL',
    created_at datetime default CURRENT_TIMESTAMP null comment '创建时间',
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
    mac           varchar(17)                        not null comment '发帖设备MAC',
    content_text  text                               null,
    content_image varchar(512)                       null comment '图片URL',
    created_at    datetime default CURRENT_TIMESTAMP null comment '发帖时间',
    constraint fk_post_mac
        foreign key (mac) references stackChan.device (mac)
);

create table stackChan.device_post_comment
(
    id         bigint auto_increment
        primary key,
    post_id    bigint                             not null comment '帖子ID',
    mac        varchar(17)                        not null comment '评论设备MAC',
    content    text                               null,
    created_at datetime default CURRENT_TIMESTAMP null comment '评论时间',
    constraint fk_comment_mac
        foreign key (mac) references stackChan.device (mac),
    constraint fk_comment_post
        foreign key (post_id) references stackChan.device_post (id)
            on delete cascade
);

