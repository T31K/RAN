USE RanUser;
SET FOREIGN_KEY_CHECKS=0;
CREATE TABLE `AccountInfo` (
  `AccountNum` INT NOT NULL AUTO_INCREMENT,
  `BankName` VARCHAR(30) CHARACTER SET euckr,
  `BankNumber` VARCHAR(50) CHARACTER SET euckr,
  `AccountOwner` VARCHAR(30) CHARACTER SET euckr,
  KEY `idx_ai_AccountNum` (`AccountNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `BlockAddress` (
  `BlockIdx` INT NOT NULL AUTO_INCREMENT,
  `BlockAddress` VARCHAR(23) CHARACTER SET euckr NOT NULL,
  `BlockReason` VARCHAR(256) CHARACTER SET euckr,
  `BlockDate` DATETIME,
  PRIMARY KEY (`BlockIdx`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `CheckId` (
  `Num` INT NOT NULL AUTO_INCREMENT,
  `Id` VARCHAR(18) CHARACTER SET euckr NOT NULL,
  `CreateDate` DATETIME,
  PRIMARY KEY (`Id`),
  KEY `idx_ai_Num` (`Num`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `DaumLogGameTime` (
  `GameTimeNum` INT NOT NULL AUTO_INCREMENT,
  `LogDate` DATETIME NOT NULL,
  `GameTime` INT NOT NULL,
  `UserUID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `ChaNum` INT,
  `UserNum` INT NOT NULL,
  `SGNum` INT,
  `SvrNum` INT,
  KEY `idx_ai_GameTimeNum` (`GameTimeNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `DaumLogLogin` (
  `LoginNum` INT NOT NULL AUTO_INCREMENT,
  `UserNum` INT NOT NULL,
  `UserUID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `LogInOut` INT NOT NULL,
  `LogDate` DATETIME,
  `LogIpAddress` VARCHAR(23) CHARACTER SET euckr,
  KEY `idx_ai_LoginNum` (`LoginNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `DaumUserInfo` (
  `UserNum` INT NOT NULL AUTO_INCREMENT,
  `UserUID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserGID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `SSNHEAD` VARCHAR(8) CHARACTER SET euckr NOT NULL,
  `SEX` VARCHAR(3) CHARACTER SET euckr NOT NULL,
  `UserType` INT NOT NULL,
  `UserLoginState` INT NOT NULL,
  `UserAvailable` INT NOT NULL,
  `CreateDate` DATETIME NOT NULL,
  `LastLoginDate` DATETIME NOT NULL,
  `SGNum` INT,
  `SvrNum` INT,
  `ChaName` VARCHAR(33) CHARACTER SET euckr,
  `UserBlock` INT NOT NULL,
  `UserBlockDate` DATETIME NOT NULL,
  `ChaRemain` INT,
  `ChaTestRemain` INT,
  `PremiumDate` DATETIME NOT NULL,
  `ChatBlockDate` DATETIME NOT NULL,
  KEY `idx_ai_UserNum` (`UserNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Donation` (
  `Name` VARCHAR(50) CHARACTER SET euckr,
  `Tnum` BIGINT NOT NULL AUTO_INCREMENT,
  `Date` DATETIME,
  `Item` VARCHAR(50) CHARACTER SET euckr,
  `Quantity` VARCHAR(50) CHARACTER SET euckr,
  `Duration` VARCHAR(50) CHARACTER SET euckr,
  `Usernum` BIGINT,
  KEY `idx_ai_Tnum` (`Tnum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `donator_itemlst` (
  `ItemNum` INT NOT NULL AUTO_INCREMENT,
  `ItemName` VARCHAR(80) CHARACTER SET euckr,
  `ItemData` VARBINARY(200),
  KEY `idx_ai_ItemNum` (`ItemNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Fullclient` (
  `link_name` VARCHAR(100) CHARACTER SET euckr,
  `link_address` LONGTEXT,
  `link_description` LONGTEXT,
  `link_id` VARCHAR(100) CHARACTER SET euckr,
  `link_date` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `fullclientsplit` (
  `link_name` VARCHAR(100) CHARACTER SET euckr,
  `link_address` LONGTEXT,
  `link_description` LONGTEXT,
  `link_id` VARCHAR(100) CHARACTER SET euckr,
  `link_date` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `FullUserInfo` (
  `UserNum` INT NOT NULL AUTO_INCREMENT,
  `UserName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserPass` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserPass2` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `BodyID` VARCHAR(18) CHARACTER SET euckr NOT NULL,
  `Sex` VARCHAR(2) CHARACTER SET euckr NOT NULL,
  `Email` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `BirthY` VARCHAR(4) CHARACTER SET euckr NOT NULL,
  `BirthM` VARCHAR(2) CHARACTER SET euckr NOT NULL,
  `BirthD` VARCHAR(2) CHARACTER SET euckr NOT NULL,
  `TEL` VARCHAR(15) CHARACTER SET euckr NOT NULL,
  `Mobile` VARCHAR(13) CHARACTER SET euckr,
  `QQ` VARCHAR(13) CHARACTER SET euckr,
  `MSN` VARCHAR(50) CHARACTER SET euckr,
  `City1` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `City2` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `Post` VARCHAR(6) CHARACTER SET euckr,
  `Address` VARCHAR(60) CHARACTER SET euckr,
  `SafeId` VARCHAR(12) CHARACTER SET euckr NOT NULL,
  `BodyID2` VARCHAR(18) CHARACTER SET euckr,
  KEY `idx_ai_UserNum` (`UserNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `gmc` (
  `username` VARCHAR(50) CHARACTER SET utf8mb4,
  `session` LONGTEXT,
  `sesexp` LONGTEXT
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `HelpDesk` (
  `TicketNum` INT NOT NULL,
  `UserName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `Subject` VARCHAR(40) CHARACTER SET euckr NOT NULL,
  `Severity` INT NOT NULL,
  `Problem` VARCHAR(5000) CHARACTER SET euckr,
  `Date` DATETIME NOT NULL,
  `CaseStatus` INT NOT NULL,
  `Reply` VARCHAR(5000) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `htlog` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `aname` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `IPAddress` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `LogDate` DATETIME NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `IPInfo` (
  `IpAddress` VARCHAR(23) CHARACTER SET euckr NOT NULL,
  `UserNum` INT NOT NULL,
  `IdxIP` INT NOT NULL AUTO_INCREMENT,
  `UseAvailable` INT,
  PRIMARY KEY (`IdxIP`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `item` (
  `ItemID` VARCHAR(50) CHARACTER SET euckr,
  `ItemMain` VARCHAR(50) CHARACTER SET euckr,
  `ItemSub` VARCHAR(50) CHARACTER SET euckr,
  `ItemName` LONGTEXT,
  `ItemDescription` LONGTEXT,
  `ItemImage` VARCHAR(50) CHARACTER SET euckr,
  `ItemNum` BIGINT NOT NULL AUTO_INCREMENT,
  KEY `idx_ai_ItemNum` (`ItemNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogAction` (
  `ActionNum` BIGINT NOT NULL AUTO_INCREMENT,
  `ChaNum` INT NOT NULL,
  `Type` INT NOT NULL,
  `TargetNum` INT NOT NULL,
  `TargetType` INT NOT NULL,
  `BrightPoint` INT NOT NULL,
  `LifePoint` INT NOT NULL,
  `ExpPoint` DECIMAL(19,4) NOT NULL,
  `ActionMoney` DECIMAL(19,4) NOT NULL,
  `ActionDate` DATETIME NOT NULL,
  PRIMARY KEY (`ActionNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogGameTime` (
  `GameTimeNum` BIGINT NOT NULL AUTO_INCREMENT,
  `LogDate` DATETIME NOT NULL,
  `GameTime` INT NOT NULL,
  `UserID` VARCHAR(20) CHARACTER SET euckr,
  `UserNum` INT,
  `SGNum` INT,
  `SvrNum` INT,
  `ChaNum` INT,
  PRIMARY KEY (`GameTimeNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogGmCmd` (
  `GmCmdNum` INT NOT NULL AUTO_INCREMENT,
  `LogDate` DATETIME,
  `GmCmd` VARCHAR(200) CHARACTER SET euckr,
  `UserNum` INT,
  `userip` VARCHAR(50) CHARACTER SET euckr,
  PRIMARY KEY (`GmCmdNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LoginFails` (
  `IP` VARCHAR(20) CHARACTER SET euckr,
  `UserID` VARCHAR(50) CHARACTER SET euckr,
  `Time` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogLogin` (
  `LoginNum` BIGINT NOT NULL AUTO_INCREMENT,
  `UserNum` INT NOT NULL,
  `UserID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `LogInOut` INT NOT NULL,
  `LogDate` DATETIME,
  `LogIpAddress` VARCHAR(23) CHARACTER SET euckr,
  PRIMARY KEY (`LoginNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogServerState` (
  `SvrStateNum` INT NOT NULL AUTO_INCREMENT,
  `LogDate` DATETIME NOT NULL,
  `UserNum` INT NOT NULL,
  `UserMaxNum` INT NOT NULL,
  `SvrNum` INT NOT NULL,
  `SGNum` INT NOT NULL,
  PRIMARY KEY (`SvrStateNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LVList` (
  `lvid` INT NOT NULL AUTO_INCREMENT,
  `Chaskill` INT,
  `Chalevel` INT,
  `ChaRemain` INT,
  PRIMARY KEY (`lvid`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Manage` (
  `ID` INT NOT NULL AUTO_INCREMENT,
  `UserName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `UserPass` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `UserQX` VARCHAR(100) CHARACTER SET euckr,
  `online` CHAR(10) CHARACTER SET euckr,
  KEY `idx_ai_ID` (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote` (
  `vote_name` VARCHAR(100) CHARACTER SET euckr NOT NULL,
  `vote_link` VARCHAR(100) CHARACTER SET euckr NOT NULL,
  `vote_img` VARCHAR(100) CHARACTER SET euckr NOT NULL,
  `vote_date` VARCHAR(100) CHARACTER SET euckr NOT NULL,
  `vote_unid` VARCHAR(100) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote_count_acc` (
  `vote_acc` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_count` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_newdate` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_newhour` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_id` VARCHAR(50) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote_hours` (
  `vote_hours` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_how` VARCHAR(50) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote_log` (
  `memb___id` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_credits` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_resets` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_levels` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_date` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_link` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_ip` VARCHAR(50) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote_logs` (
  `memb___id` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_what` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_date` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_link` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_alink` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_ip` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `unid` VARCHAR(50) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `MuWeb_des_vote_set` (
  `vote_credit` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_credits` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_reset` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_resets` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_level` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_levels` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_lvlup` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `vote_lvlupmg` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `max_level` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `max_reset` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `max_credits` VARCHAR(50) CHARACTER SET euckr NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `newcheckid` (
  `Id` VARCHAR(18) CHARACTER SET euckr NOT NULL,
  `CreateDate` DATETIME
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `NewType` (
  `id` INT NOT NULL,
  `TypeName` VARCHAR(50) CHARACTER SET utf8mb4 NOT NULL,
  `lx` VARCHAR(10) CHARACTER SET utf8mb4 NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `PassCheckLog` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `ts` DATETIME,
  `uid` VARCHAR(50) CHARACTER SET euckr,
  `pw` VARCHAR(50) CHARACTER SET euckr,
  `nation` INT,
  `ret` INT,
  KEY `idx_ai_id` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `patches` (
  `link_name` VARCHAR(100) CHARACTER SET euckr,
  `link_address` LONGTEXT,
  `link_description` LONGTEXT,
  `link_id` VARCHAR(100) CHARACTER SET euckr,
  `link_date` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `plusx` (
  `PlusID` INT,
  `Plus` VARCHAR(50) CHARACTER SET euckr,
  `Hex` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `rvslot` (
  `id` INT,
  `rvslot` VARCHAR(50) CHARACTER SET euckr,
  `hex` VARCHAR(50) CHARACTER SET euckr,
  `hexnum` VARCHAR(2) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ServerGroup` (
  `SGNum` INT NOT NULL,
  `SGName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `OdbcName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `OdbcUserID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `OdbcPassword` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `OdbcLogName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `OdbcLogUserID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `OdbcLogPassword` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`SGNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ServerInfo` (
  `SGNum` INT NOT NULL,
  `SvrNum` INT NOT NULL,
  `SvrType` INT NOT NULL,
  PRIMARY KEY (`SvrNum`,`SGNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopGoumaiInfo` (
  `ID` INT NOT NULL AUTO_INCREMENT,
  `ChaNum` INT NOT NULL,
  `UserNum` INT NOT NULL,
  `UserID` VARCHAR(50) CHARACTER SET euckr,
  `GMID` INT NOT NULL,
  `ItemName` VARCHAR(100) CHARACTER SET utf8mb4,
  `MainID` INT NOT NULL,
  `usbID` INT NOT NULL,
  `ItemPoint` BIGINT NOT NULL,
  `ItemDate` DATETIME,
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `skill` (
  `SDN` VARCHAR(50) CHARACTER SET euckr,
  `SkillMain` VARCHAR(50) CHARACTER SET euckr,
  `SkillSub` VARCHAR(50) CHARACTER SET euckr,
  `SkillName` VARCHAR(100) CHARACTER SET euckr,
  `SkillImage` VARCHAR(50) CHARACTER SET euckr,
  `SkillDescription` LONGTEXT,
  `SkillType` INT,
  `SkillStyle` INT
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Skills` (
  `SkillNum` INT NOT NULL AUTO_INCREMENT,
  `SkillMain` VARCHAR(50) CHARACTER SET euckr,
  `SkillSub` VARCHAR(50) CHARACTER SET euckr,
  `SkillName` VARCHAR(50) CHARACTER SET euckr,
  `SkillImage` VARCHAR(50) CHARACTER SET euckr,
  `SkillDescription` VARCHAR(150) CHARACTER SET euckr,
  `SkillType` INT,
  `SkillChar` INT,
  `SkillCost` INT NOT NULL,
  `SkillKey` VARCHAR(50) CHARACTER SET euckr,
  `SkillReq` VARCHAR(50) CHARACTER SET euckr,
  `SkillLevel` INT,
  `SkillGender` INT,
  KEY `idx_ai_SkillNum` (`SkillNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `SqlInjectionLog` (
  `LastDate` VARCHAR(50) CHARACTER SET euckr,
  `SqlInject` VARCHAR(50) CHARACTER SET euckr,
  `HostIp` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `StatGameTime` (
  `GYear` INT NOT NULL,
  `GMonth` INT NOT NULL,
  `GDay` INT NOT NULL,
  `GTime` INT,
  PRIMARY KEY (`GYear`,`GMonth`,`GDay`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `StatLogin` (
  `LYear` INT NOT NULL,
  `LMonth` INT NOT NULL,
  `LDay` INT NOT NULL,
  `LHour` INT NOT NULL,
  `LCount` INT,
  PRIMARY KEY (`LYear`,`LMonth`,`LDay`,`LHour`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `sysdiagrams` (
  `name` VARCHAR(128) CHARACTER SET utf8mb4 NOT NULL,
  `principal_id` INT NOT NULL,
  `diagram_id` INT NOT NULL AUTO_INCREMENT,
  `version` INT,
  `definition` LONGBLOB,
  PRIMARY KEY (`diagram_id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `TopUp` (
  `Code` VARCHAR(20) CHARACTER SET euckr,
  `CodePinCode` VARCHAR(20) CHARACTER SET euckr,
  `CodeValue` INT
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `traceip` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `ua` VARCHAR(255) CHARACTER SET euckr NOT NULL,
  `ip` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  `datetime` INT NOT NULL,
  `cpt` INT NOT NULL,
  `locked` SMALLINT NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `traceip_cfg` (
  `var` VARCHAR(255) CHARACTER SET euckr NOT NULL,
  `val` VARCHAR(255) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`var`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `TransactionLogs` (
  `TransactionID` INT NOT NULL,
  `UserName` VARCHAR(50) CHARACTER SET euckr,
  `FunctionRecord` VARCHAR(50) CHARACTER SET euckr,
  `DateRecord` VARCHAR(50) CHARACTER SET euckr,
  `PointsCost` VARCHAR(50) CHARACTER SET euckr,
  `PointsAdd` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserInfo` (
  `UserNum` INT NOT NULL AUTO_INCREMENT,
  `UserName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserPass` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserPass2` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `UserType` INT NOT NULL,
  `UserLoginState` INT NOT NULL,
  `UserAvailable` INT,
  `CreateDate` DATETIME NOT NULL,
  `LastLoginDate` DATETIME NOT NULL,
  `EndDate` DATETIME,
  `UserAge` INT,
  `OfflineTime` INT NOT NULL,
  `GameTime` INT,
  `SGNum` INT,
  `SvrNum` INT,
  `ChaName` VARCHAR(33) CHARACTER SET euckr,
  `UserBlock` INT NOT NULL,
  `UserBlockDate` DATETIME NOT NULL,
  `ChaRemain` INT NOT NULL,
  `ChaTestRemain` INT NOT NULL,
  `PremiumDate` DATETIME NOT NULL,
  `ChatBlockDate` DATETIME NOT NULL,
  `NewUserPass` VARCHAR(50) CHARACTER SET euckr,
  `NewUserPass1` VARCHAR(50) CHARACTER SET euckr,
  `tj` VARCHAR(50) CHARACTER SET euckr,
  `online` VARCHAR(50) CHARACTER SET euckr,
  `Email` VARCHAR(50) CHARACTER SET euckr,
  `PinCodes` VARCHAR(50) CHARACTER SET euckr,
  `IPCount` INT,
  `UserEmail` VARCHAR(50) CHARACTER SET euckr,
  `UserPoint` INT,
  `WebLoginState` INT NOT NULL,
  `Upass` VARCHAR(50) CHARACTER SET euckr,
  `IpSite` VARCHAR(50) CHARACTER SET euckr,
  `Donated` VARCHAR(50) CHARACTER SET euckr,
  `GameTime2` INT NOT NULL,
  `UserVIP` INT NOT NULL,
  `UserPoint2` INT NOT NULL,
  PRIMARY KEY (`UserNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserLastInfo` (
  `UserNum` INT,
  `UserMoney` DECIMAL(19,4)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserPoint` (
  `UserNum` INT NOT NULL,
  `Point` INT NOT NULL,
  PRIMARY KEY (`UserNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserPointLog` (
  `ID` INT NOT NULL AUTO_INCREMENT,
  `userNum` BIGINT,
  `Point` BIGINT,
  `updateTime` DATETIME NOT NULL,
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VLog` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `ts` DATETIME,
  `uid` VARCHAR(50) CHARACTER SET euckr,
  `pw` VARCHAR(50) CHARACTER SET euckr,
  `pwlen` INT,
  `stored` VARCHAR(50) CHARACTER SET euckr,
  `matched` INT,
  KEY `idx_ai_id` (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Vote` (
  `id` INT NOT NULL AUTO_INCREMENT,
  `link` VARCHAR(100) CHARACTER SET euckr,
  `username` VARCHAR(50) CHARACTER SET euckr,
  `votetime` INT,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VoteAccountLog` (
  `VoteUserName` VARCHAR(50) CHARACTER SET euckr,
  `VoteCount` VARCHAR(50) CHARACTER SET euckr,
  `VoteDate` VARCHAR(50) CHARACTER SET euckr,
  `VoteHour` VARCHAR(50) CHARACTER SET euckr,
  `VoteID` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VoteCountLog` (
  `VoteLog` VARCHAR(50) CHARACTER SET euckr,
  `VoteCount` VARCHAR(50) CHARACTER SET euckr,
  `VoteDate` VARCHAR(50) CHARACTER SET euckr,
  `VoteID` INT
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VoteLink` (
  `VoteName` VARCHAR(100) CHARACTER SET euckr,
  `VoteLink` VARCHAR(100) CHARACTER SET euckr,
  `VoteImg` VARCHAR(100) CHARACTER SET euckr,
  `VoteDate` VARCHAR(100) CHARACTER SET euckr,
  `VoteID` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VoteSettingTime` (
  `VoteHour` INT,
  `VoteCategory` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web` (
  `webtitle` VARCHAR(100) CHARACTER SET euckr,
  `resetmoney` VARCHAR(100) CHARACTER SET euckr,
  `pkmoney` VARCHAR(100) CHARACTER SET euckr,
  `resetlevel` VARCHAR(100) CHARACTER SET euckr,
  `servername` VARCHAR(100) CHARACTER SET euckr,
  `client` VARCHAR(100) CHARACTER SET euckr,
  `patch` VARCHAR(100) CHARACTER SET euckr,
  `launcher` VARCHAR(100) CHARACTER SET euckr,
  `serverwebsite` VARCHAR(100) CHARACTER SET euckr,
  `resetpoints` VARCHAR(100) CHARACTER SET euckr,
  `resetslimit` VARCHAR(100) CHARACTER SET euckr,
  `resetmode` VARCHAR(100) CHARACTER SET euckr,
  `levelupmode` VARCHAR(100) CHARACTER SET euckr,
  `announcements` LONGTEXT,
  `forum` VARCHAR(100) CHARACTER SET euckr,
  `ads` LONGTEXT,
  `clean_inventory` VARCHAR(100) CHARACTER SET euckr,
  `clean_skills` VARCHAR(100) CHARACTER SET euckr,
  `store_inbox` VARCHAR(100) CHARACTER SET euckr,
  `store_sent` VARCHAR(100) CHARACTER SET euckr,
  `msg_length` VARCHAR(100) CHARACTER SET euckr,
  `md5` VARCHAR(50) CHARACTER SET euckr,
  `show_gm` VARCHAR(100) CHARACTER SET euckr,
  `template` VARCHAR(50) CHARACTER SET euckr,
  `warp_zen` VARCHAR(50) CHARACTER SET euckr,
  `music` VARCHAR(50) CHARACTER SET euckr,
  `header` VARCHAR(50) CHARACTER SET euckr,
  `reset_stat` VARCHAR(50) CHARACTER SET euckr,
  `reset_skill` VARCHAR(50) CHARACTER SET euckr,
  `reset_quest` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_admin` (
  `username` VARCHAR(100) CHARACTER SET euckr,
  `password` VARCHAR(100) CHARACTER SET euckr,
  `securitycode` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_links` (
  `link_name` VARCHAR(100) CHARACTER SET euckr,
  `link_address` LONGTEXT,
  `link_description` LONGTEXT,
  `link_id` VARCHAR(100) CHARACTER SET euckr,
  `link_date` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_mail` (
  `to_character` VARCHAR(100) CHARACTER SET euckr,
  `from_character` VARCHAR(100) CHARACTER SET euckr,
  `accountid` VARCHAR(100) CHARACTER SET euckr,
  `subject` VARCHAR(100) CHARACTER SET euckr,
  `mail_msg` VARCHAR(1000) CHARACTER SET euckr,
  `inbox` VARCHAR(100) CHARACTER SET euckr,
  `sent` VARCHAR(100) CHARACTER SET euckr,
  `read_msg` VARCHAR(100) CHARACTER SET euckr,
  `date` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_mail_store` (
  `accountid` VARCHAR(100) CHARACTER SET euckr,
  `store_inbox` VARCHAR(100) CHARACTER SET euckr,
  `store_sent` VARCHAR(100) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_news` (
  `news_id` INT NOT NULL AUTO_INCREMENT,
  `news_title` VARCHAR(100) CHARACTER SET euckr,
  `news_autor` VARCHAR(100) CHARACTER SET euckr,
  `news_category` VARCHAR(100) CHARACTER SET euckr,
  `news_context` LONGTEXT,
  `news_date` VARCHAR(100) CHARACTER SET euckr,
  PRIMARY KEY (`news_id`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `Web_servers` (
  `name` VARCHAR(100) CHARACTER SET euckr,
  `experience` VARCHAR(100) CHARACTER SET euckr,
  `drops` VARCHAR(100) CHARACTER SET euckr,
  `gsport` VARCHAR(100) CHARACTER SET euckr,
  `ip` VARCHAR(100) CHARACTER SET euckr,
  `display_order` VARCHAR(100) CHARACTER SET euckr,
  `version` VARCHAR(100) CHARACTER SET euckr,
  `type` VARCHAR(50) CHARACTER SET euckr
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

