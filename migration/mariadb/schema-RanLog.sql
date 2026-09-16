USE RanLog;
SET FOREIGN_KEY_CHECKS=0;
CREATE TABLE `HackProgramList` (
  `HackProgramNum` INT NOT NULL,
  `HackProgramName` VARCHAR(100) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`HackProgramNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ItemList` (
  `ProductNum` INT NOT NULL,
  `ItemMain` INT NOT NULL,
  `ItemSub` INT NOT NULL,
  `ItemName` VARCHAR(100) CHARACTER SET euckr
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

CREATE TABLE `LogExchangeFlag` (
  `ExchangeFlag` INT NOT NULL,
  `ExchangeName` VARCHAR(50) CHARACTER SET euckr,
  PRIMARY KEY (`ExchangeFlag`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogHackProgram` (
  `HackNum` INT NOT NULL AUTO_INCREMENT,
  `UserNum` INT NOT NULL,
  `ChaNum` INT NOT NULL,
  `SGNum` INT NOT NULL,
  `SvrNum` INT NOT NULL,
  `HackProgramNum` INT NOT NULL,
  `HackDate` DATETIME NOT NULL,
  `HackComment` VARCHAR(512) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`HackNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogItemExchange` (
  `ExchangeNum` BIGINT NOT NULL AUTO_INCREMENT,
  `NIDMain` INT,
  `NIDSub` INT,
  `SGNum` INT,
  `SvrNum` INT,
  `FldNum` INT,
  `MakeType` INT NOT NULL,
  `MakeNum` DECIMAL(19,4) NOT NULL,
  `ItemAmount` INT NOT NULL,
  `ItemFromFlag` INT NOT NULL,
  `ItemFrom` INT NOT NULL,
  `ItemToFlag` INT NOT NULL,
  `ItemTo` INT NOT NULL,
  `ExchangeFlag` INT NOT NULL,
  `ExchangeDate` DATETIME NOT NULL,
  `Damage` INT NOT NULL,
  `Defense` INT NOT NULL,
  `Fire` INT NOT NULL,
  `Ice` INT NOT NULL,
  `Poison` INT NOT NULL,
  `Electric` INT NOT NULL,
  `Spirit` INT NOT NULL,
  `CostumeMID` INT NOT NULL,
  `CostumeSID` INT NOT NULL,
  `TradePrice` DECIMAL(19,4) NOT NULL,
  PRIMARY KEY (`ExchangeNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogItemMax` (
  `NIDMain` INT NOT NULL,
  `NIDSub` INT NOT NULL,
  `SGNum` INT NOT NULL,
  `SvrNum` INT NOT NULL,
  `FldNum` INT NOT NULL,
  `MakeType` INT NOT NULL,
  `MaxNum` DECIMAL(19,4),
  PRIMARY KEY (`NIDMain`,`NIDSub`,`SGNum`,`SvrNum`,`FldNum`,`MakeType`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogItemRandom` (
  `RandomNum` BIGINT NOT NULL AUTO_INCREMENT,
  `NIDMain` SMALLINT,
  `NIDSub` SMALLINT,
  `SGNum` TINYINT UNSIGNED,
  `SvrNum` TINYINT UNSIGNED,
  `FldNum` TINYINT UNSIGNED,
  `MakeType` TINYINT UNSIGNED NOT NULL,
  `MakeNum` DECIMAL(19,4) NOT NULL,
  `RandomType1` TINYINT UNSIGNED NOT NULL,
  `RandomValue1` SMALLINT NOT NULL,
  `RandomType2` TINYINT UNSIGNED NOT NULL,
  `RandomValue2` SMALLINT NOT NULL,
  `RandomType3` TINYINT UNSIGNED NOT NULL,
  `RandomValue3` SMALLINT NOT NULL,
  `RandomType4` TINYINT UNSIGNED NOT NULL,
  `RandomValue4` SMALLINT NOT NULL,
  `RandomDate` DATETIME NOT NULL,
  PRIMARY KEY (`RandomNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogMakeType` (
  `MakeType` INT NOT NULL,
  `MakeName` VARCHAR(20) CHARACTER SET euckr,
  PRIMARY KEY (`MakeType`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogPetAction` (
  `ActionNum` BIGINT NOT NULL AUTO_INCREMENT,
  `PetNum` INT NOT NULL,
  `ItemMID` SMALLINT NOT NULL,
  `ItemSID` SMALLINT NOT NULL,
  `ActionType` SMALLINT NOT NULL,
  `PetFull` INT NOT NULL,
  `LogDate` DATETIME NOT NULL,
  PRIMARY KEY (`ActionNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogPetActionFlag` (
  `PetActionFlag` INT NOT NULL,
  `PetActionName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`PetActionFlag`)
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

CREATE TABLE `LogShopPurchase` (
  `PurNum` BIGINT NOT NULL AUTO_INCREMENT,
  `PurKey` VARCHAR(21) CHARACTER SET euckr NOT NULL,
  `PurFlag` INT,
  `PurDate` DATETIME,
  PRIMARY KEY (`PurNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogVehicleAction` (
  `ActionNum` BIGINT NOT NULL AUTO_INCREMENT,
  `VehicleNum` INT NOT NULL,
  `ItemMID` SMALLINT NOT NULL,
  `ItemSID` SMALLINT NOT NULL,
  `ActionType` SMALLINT NOT NULL,
  `VehicleBattery` INT NOT NULL,
  `LogDate` DATETIME NOT NULL,
  PRIMARY KEY (`ActionNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `LogVehicleActionFlag` (
  `VehicleActionFlag` INT NOT NULL,
  `VehicleActionName` VARCHAR(50) CHARACTER SET euckr NOT NULL,
  PRIMARY KEY (`VehicleActionFlag`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopItemMap` (
  `ProductNum` INT NOT NULL,
  `ItemMain` INT,
  `ItemSub` INT,
  `ItemName` VARCHAR(100) CHARACTER SET euckr,
  PRIMARY KEY (`ProductNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopPurchase` (
  `PurKey` VARCHAR(21) CHARACTER SET euckr NOT NULL,
  `UserUID` VARCHAR(6) CHARACTER SET euckr NOT NULL,
  `ProductNum` INT NOT NULL,
  `PurPrice` INT NOT NULL,
  `PurFlag` INT NOT NULL,
  `PurDate` DATETIME NOT NULL,
  `PurChgDate` DATETIME,
  PRIMARY KEY (`PurKey`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopPurFlag` (
  `PurFlag` INT NOT NULL,
  `PurFlagName` VARCHAR(20) CHARACTER SET euckr,
  PRIMARY KEY (`PurFlag`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `SkillList` (
  `SkillMain` INT NOT NULL,
  `SkillSub` INT NOT NULL,
  `SkillName` VARCHAR(30) CHARACTER SET euckr,
  `SkillGrade` INT,
  PRIMARY KEY (`SkillMain`,`SkillSub`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

