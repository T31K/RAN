USE RanGame1;
SET FOREIGN_KEY_CHECKS=0;
CREATE TABLE `ChaFriend` (
  `ChaFriendNum` INT NOT NULL AUTO_INCREMENT,
  `ChaP` INT NOT NULL,
  `ChaS` INT NOT NULL,
  `ChaFlag` INT NOT NULL,
  PRIMARY KEY (`ChaFriendNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ChaFriendBackup` (
  `ChaFriendNum` INT NOT NULL AUTO_INCREMENT,
  `ChaP` INT NOT NULL,
  `ChaS` INT NOT NULL,
  `ChaFlag` INT NOT NULL,
  KEY `idx_ai_ChaFriendNum` (`ChaFriendNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ChaInfo` (
  `ChaNum` INT NOT NULL AUTO_INCREMENT,
  `SGNum` INT,
  `UserNum` INT NOT NULL,
  `GuNum` INT,
  `GuPosition` INT NOT NULL,
  `ChaName` VARCHAR(33) CHARACTER SET euckr NOT NULL,
  `ChaGuName` VARCHAR(33) CHARACTER SET euckr NOT NULL,
  `ChaTribe` INT NOT NULL,
  `ChaClass` INT NOT NULL,
  `ChaSchool` INT NOT NULL,
  `ChaSex` TINYINT UNSIGNED,
  `ChaHair` INT NOT NULL,
  `ChaHairColor` INT,
  `ChaFace` INT NOT NULL,
  `ChaLiving` INT NOT NULL,
  `ChaLevel` INT NOT NULL,
  `ChaMoney` DECIMAL(19,4) NOT NULL,
  `ChaPower` BIGINT NOT NULL,
  `ChaStrong` BIGINT NOT NULL,
  `ChaStrength` BIGINT NOT NULL,
  `ChaSpirit` BIGINT NOT NULL,
  `ChaDex` BIGINT NOT NULL,
  `ChaIntel` INT NOT NULL,
  `ChaStRemain` BIGINT NOT NULL,
  `ChaExp` DECIMAL(19,4) NOT NULL,
  `ChaViewRange` BIGINT NOT NULL,
  `ChaHP` BIGINT NOT NULL,
  `ChaMP` BIGINT NOT NULL,
  `ChaStartMap` INT NOT NULL,
  `ChaStartGate` INT NOT NULL,
  `ChaPosX` DOUBLE NOT NULL,
  `ChaPosY` DOUBLE NOT NULL,
  `ChaPosZ` DOUBLE NOT NULL,
  `ChaSaveMap` INT NOT NULL,
  `ChaSavePosX` DOUBLE NOT NULL,
  `ChaSavePosY` DOUBLE NOT NULL,
  `ChaSavePosZ` DOUBLE NOT NULL,
  `ChaReturnMap` INT NOT NULL,
  `ChaReturnPosX` DOUBLE NOT NULL,
  `ChaReturnPosY` DOUBLE NOT NULL,
  `ChaReturnPosZ` DOUBLE NOT NULL,
  `ChaBright` INT NOT NULL,
  `ChaAttackP` INT NOT NULL,
  `ChaDefenseP` INT NOT NULL,
  `ChaFightA` INT NOT NULL,
  `ChaShootA` INT NOT NULL,
  `ChaSP` BIGINT NOT NULL,
  `ChaPK` INT NOT NULL,
  `ChaSkillPoint` INT NOT NULL,
  `ChaInvenLine` INT NOT NULL,
  `ChaDeleted` INT NOT NULL,
  `ChaOnline` INT NOT NULL,
  `ChaCreateDate` DATETIME NOT NULL,
  `ChaDeletedDate` DATETIME NOT NULL,
  `ChaStorage2` DATETIME NOT NULL,
  `ChaStorage3` DATETIME NOT NULL,
  `ChaStorage4` DATETIME NOT NULL,
  `ChaGuSecede` DATETIME NOT NULL,
  `ChaQuest` LONGBLOB,
  `ChaSkills` LONGBLOB,
  `ChaSkillSlot` LONGBLOB,
  `ChaActionSlot` LONGBLOB,
  `ChaPutOnItems` LONGBLOB,
  `ChaInven` LONGBLOB,
  `ChaReExp` DOUBLE,
  `ChaReborn` INT NOT NULL,
  `ChaSpecial` SMALLINT,
  `ChaKill` INT NOT NULL,
  `ChaRebornReset` INT NOT NULL,
  `ChangeClass` INT NOT NULL,
  `ChaKills` INT NOT NULL,
  `GraveYard` INT NOT NULL,
  `ChaCoolTime` LONGBLOB,
  `FixInject` VARCHAR(33) CHARACTER SET euckr,
  `AuctionTimeOut` VARBINARY(50),
  `ChaSpSID` INT,
  `ChaSpMID` INT,
  `ChaCP` INT,
  `SumSub` INT,
  `SumMain` INT,
  `UserLastInfo` INT,
  PRIMARY KEY (`ChaNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ChaLastInfo` (
  `ChaNum` INT NOT NULL,
  `ChaLevel` INT NOT NULL,
  `ChaMoney` DECIMAL(19,4) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ChaNameInfo` (
  `ChaNameNum` INT NOT NULL AUTO_INCREMENT,
  `ChaNum` INT NOT NULL,
  `ChaName` VARCHAR(33) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`ChaNameNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `GuildAlliance` (
  `GuNumP` INT NOT NULL,
  `GuNumS` INT NOT NULL,
  PRIMARY KEY (`GuNumP`,`GuNumS`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `GuildBattle` (
  `GuBattleNum` INT NOT NULL AUTO_INCREMENT,
  `GuSNum` INT,
  `GuPNum` INT,
  `GuAlliance` INT,
  `GuFlag` INT,
  `GuKillNum` INT,
  `GuDeathNum` INT,
  `GuBattleStartDate` DATETIME,
  `GuBattleEndDate` DATETIME,
  KEY `idx_ai_GuBattleNum` (`GuBattleNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `GuildInfo` (
  `GuNum` INT NOT NULL AUTO_INCREMENT,
  `ChaNum` INT NOT NULL,
  `GuDeputy` INT NOT NULL,
  `GuName` VARCHAR(33) CHARACTER SET euckr NOT NULL,
  `GuNotice` VARCHAR(401) CHARACTER SET euckr NOT NULL,
  `GuRank` INT NOT NULL,
  `GuMoney` DECIMAL(19,4) NOT NULL,
  `GuIncomeMoney` DECIMAL(19,4) NOT NULL,
  `GuMarkVer` INT NOT NULL,
  `GuExpire` INT NOT NULL,
  `GuMakeTime` DATETIME NOT NULL,
  `GuExpireTime` DATETIME NOT NULL,
  `GuAllianceSec` DATETIME NOT NULL,
  `GuAllianceDis` DATETIME NOT NULL,
  `GuMarkImage` LONGBLOB NOT NULL,
  `GuStorage` LONGBLOB NOT NULL,
  `GuAuthorityTime` DATETIME NOT NULL,
  `GuAllianceBattleLose` INT NOT NULL,
  `GuAllianceBattleDraw` INT NOT NULL,
  `GuAllianceBattleWin` INT NOT NULL,
  `GuBattleLastTime` DATETIME NOT NULL,
  `GuBattleLose` INT NOT NULL,
  `GuBattleDraw` INT NOT NULL,
  `GuBattleWin` INT NOT NULL,
  PRIMARY KEY (`GuNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `GuildRegion` (
  `RegionID` INT NOT NULL,
  `GuNum` INT NOT NULL,
  `RegionTax` DOUBLE NOT NULL,
  PRIMARY KEY (`RegionID`,`GuNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ItemCustomize` (
  `GMID` INT NOT NULL AUTO_INCREMENT,
  `MainID` INT NOT NULL,
  `SubID` INT NOT NULL,
  `ItemName` VARCHAR(255) CHARACTER SET utf8mb4,
  `ChangeQuantity` TINYINT(1) NOT NULL,
  `AttackUpgrade` TINYINT(1) NOT NULL,
  `DefenceUpgrade` TINYINT(1) NOT NULL,
  `ChaInven` LONGBLOB,
  `Point` BIGINT NOT NULL,
  `AddDate` INT NOT NULL,
  `GmType` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `EditBol` TINYINT(1) NOT NULL,
  `GfInfo` VARCHAR(255) CHARACTER SET euckr NOT NULL,
  `GmShuXing` VARCHAR(255) CHARACTER SET euckr NOT NULL,
  `GmPic` VARCHAR(100) CHARACTER SET euckr,
  `AddBol` TINYINT(1) NOT NULL,
  PRIMARY KEY (`GMID`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `PetInfo` (
  `PetNum` INT NOT NULL,
  `PetName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `PetChaNum` INT NOT NULL,
  `PetType` INT NOT NULL,
  `PetMID` INT NOT NULL,
  `PetSID` INT NOT NULL,
  `PetStyle` INT NOT NULL,
  `PetColor` INT NOT NULL,
  `PetFull` INT NOT NULL,
  `PetDeleted` INT NOT NULL,
  `PetCreateDate` DATETIME NOT NULL,
  `PetDeletedDate` DATETIME NOT NULL,
  `PetPutOnItems` LONGBLOB NOT NULL,
  `PetCardMID` INT NOT NULL,
  `PetCardSID` INT NOT NULL,
  `PetUniqueNum` INT NOT NULL AUTO_INCREMENT,
  `PetSkinStartDate` DATETIME,
  `PetSkinTime` DATETIME,
  `PetSkinScale` INT,
  `PetSkinSID` INT,
  `PetSkinMID` INT,
  PRIMARY KEY (`PetUniqueNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `PetInven` (
  `PetInvenNum` INT NOT NULL AUTO_INCREMENT,
  `PetNum` INT NOT NULL,
  `PetInvenType` INT NOT NULL,
  `PetInvenMID` INT NOT NULL,
  `PetInvenSID` INT NOT NULL,
  `PetInvenCMID` INT NOT NULL,
  `PetInvenCSID` INT NOT NULL,
  `PetInvenAvailable` INT NOT NULL,
  `PetInvenUpdateDate` DATETIME NOT NULL,
  `PetChaNum` INT NOT NULL,
  PRIMARY KEY (`PetInvenNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `PetStyleFlag` (
  `PetStyle` INT NOT NULL,
  `PetStyleName` VARCHAR(2) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`PetStyle`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `PetTypeFlag` (
  `PetType` INT NOT NULL,
  `PetName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`PetType`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `SkillTableRef` (
  `SkillNo` INT NOT NULL AUTO_INCREMENT,
  `MID` INT NOT NULL,
  `SID` INT NOT NULL,
  `SkillName` CHAR(50) CHARACTER SET utf8mb4 NOT NULL,
  `SkillDes` CHAR(200) CHARACTER SET utf8mb4,
  `SkillClass` INT,
  `SkillLevelReq` INT,
  `SkillDexReq` INT,
  `SkillStrongReq` INT,
  `SkillIntReq` INT,
  PRIMARY KEY (`SkillNo`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserInven` (
  `UserInvenNum` INT NOT NULL AUTO_INCREMENT,
  `UserNum` INT NOT NULL,
  `SGNum` INT NOT NULL,
  `ChaStorage2` DATETIME NOT NULL,
  `ChaStorage3` DATETIME NOT NULL,
  `ChaStorage4` DATETIME NOT NULL,
  `UserMoney` DECIMAL(19,4) NOT NULL,
  `UserInven` LONGBLOB,
  PRIMARY KEY (`UserInvenNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `UserLastInfo` (
  `UserNum` INT NOT NULL,
  `UserMoney` DECIMAL(19,4) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `VehicleInfo` (
  `VehicleUniqueNum` INT NOT NULL AUTO_INCREMENT,
  `VehicleNum` INT NOT NULL,
  `VehicleName` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `VehicleChaNum` INT NOT NULL,
  `VehicleType` INT NOT NULL,
  `VehicleCardMID` INT NOT NULL,
  `VehicleCardSID` INT NOT NULL,
  `VehicleBattery` INT NOT NULL,
  `VehicleDeleted` INT NOT NULL,
  `VehicleCreateDate` DATETIME NOT NULL,
  `VehicleDeletedDate` DATETIME NOT NULL,
  `VehiclePutOnItems` LONGBLOB NOT NULL,
  PRIMARY KEY (`VehicleUniqueNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

