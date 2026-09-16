USE RanShop;
SET FOREIGN_KEY_CHECKS=0;
CREATE TABLE `LogShopPurchase` (
  `PurNum` BIGINT NOT NULL AUTO_INCREMENT,
  `PurKey` VARCHAR(21) CHARACTER SET euckr NOT NULL,
  `PurFlag` INT,
  `PurDate` DATETIME,
  PRIMARY KEY (`PurNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopItemMap` (
  `ProductNum` INT NOT NULL AUTO_INCREMENT,
  `ItemMain` INT,
  `ItemSub` INT,
  `ItemName` VARCHAR(100) CHARACTER SET euckr,
  `ItemMoney` INT NOT NULL,
  `ItemType` INT,
  `ItemComment` VARCHAR(300) CHARACTER SET euckr,
  `Duration` VARCHAR(50) CHARACTER SET euckr,
  `Category` INT,
  `ItemStock` INT NOT NULL,
  `ItemImage` VARCHAR(300) CHARACTER SET euckr,
  `ItemList` VARCHAR(100) CHARACTER SET euckr,
  PRIMARY KEY (`ProductNum`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopPurchase` (
  `PurKey` INT NOT NULL AUTO_INCREMENT,
  `UserUID` VARCHAR(20) CHARACTER SET euckr NOT NULL,
  `ProductNum` INT NOT NULL,
  `PurPrice` INT NOT NULL,
  `PurFlag` INT NOT NULL,
  `PurDate` DATETIME NOT NULL,
  `PurChgDate` DATETIME,
  KEY `idx_ai_PurKey` (`PurKey`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

CREATE TABLE `ShopPurFlag` (
  `PurFlag` INT NOT NULL,
  `PurFlagName` VARCHAR(20) CHARACTER SET euckr,
  PRIMARY KEY (`PurFlag`)
) ENGINE=InnoDB DEFAULT CHARSET=euckr;

