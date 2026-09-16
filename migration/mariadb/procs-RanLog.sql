-- RanLog stored procedures ported MSSQL -> MariaDB. Params p_, locals v_.
USE RanLog;

DROP PROCEDURE IF EXISTS InsertLogHackProgram;
DROP PROCEDURE IF EXISTS log_serverstate;
DROP PROCEDURE IF EXISTS sp_LogAction_Insert;
DROP PROCEDURE IF EXISTS sp_LogItemRandom_Insert;
DROP PROCEDURE IF EXISTS sp_LogPetAction_Insert;
DROP PROCEDURE IF EXISTS sp_LogVehicleAction_Insert;
DROP PROCEDURE IF EXISTS sp_logitemexchange_insert;
DROP PROCEDURE IF EXISTS sp_purchase_change_state;

DELIMITER $$

CREATE PROCEDURE InsertLogHackProgram(
  IN p_nSGNum INT, IN p_nSvrNum INT, IN p_nUserNum INT, IN p_nChaNum INT,
  IN p_nHackProgramNum INT, IN p_strComment VARCHAR(512), OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  INSERT INTO LogHackProgram (SGNum, SvrNum, UserNum, ChaNum, HackProgramNum, HackComment)
    VALUES (p_nSGNum, p_nSvrNum, p_nUserNum, p_nChaNum, p_nHackProgramNum, p_strComment);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN SET p_nReturn = -1; ELSE SET p_nReturn = 0; END IF;
END$$

CREATE PROCEDURE log_serverstate(IN p_usernum INT, IN p_usermax INT, IN p_svrnum INT, IN p_sgnum INT)
BEGIN
  INSERT INTO LogServerState (UserNum, UserMaxNum, SvrNum, SGNum)
    VALUES (p_usernum, p_usermax, p_svrnum, p_sgnum);
END$$

CREATE PROCEDURE sp_LogAction_Insert(
  IN p_nChaNum INT, IN p_nType INT, IN p_nTargetNum INT, IN p_nTargetType INT,
  IN p_nExpPoint DECIMAL(19,4), IN p_nBrightPoint INT, IN p_nLifePoint INT, IN p_nMoney DECIMAL(19,4))
BEGIN
  IF p_nType = 2 THEN
    UPDATE RanGame1.ChaInfo SET ChaKills=ChaKills+1 WHERE ChaNum=p_nChaNum;
  END IF;
  INSERT INTO LogAction (ChaNum, Type, TargetNum, TargetType, ExpPoint, BrightPoint, LifePoint, ActionMoney)
    VALUES (p_nChaNum, p_nType, p_nTargetNum, p_nTargetType, p_nExpPoint, p_nBrightPoint, p_nLifePoint, p_nMoney);
END$$

CREATE PROCEDURE sp_LogItemRandom_Insert(
  IN p_NIDMain INT, IN p_NIDSub INT, IN p_SGNum INT, IN p_SvrNum INT, IN p_FldNum INT,
  IN p_MakeType INT, IN p_MakeNum DECIMAL(19,4),
  IN p_RandomType1 INT, IN p_RandomValue1 INT, IN p_RandomType2 INT, IN p_RandomValue2 INT,
  IN p_RandomType3 INT, IN p_RandomValue3 INT, IN p_RandomType4 INT, IN p_RandomValue4 INT,
  OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  INSERT INTO LogItemRandom (NIDMain, NIDSub, SGNum, SvrNum, FldNum, MakeType, MakeNum,
    RandomType1, RandomValue1, RandomType2, RandomValue2, RandomType3, RandomValue3, RandomType4, RandomValue4)
    VALUES (p_NIDMain, p_NIDSub, p_SGNum, p_SvrNum, p_FldNum, p_MakeType, p_MakeNum,
    p_RandomType1, p_RandomValue1, p_RandomType2, p_RandomValue2, p_RandomType3, p_RandomValue3, p_RandomType4, p_RandomValue4);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    COMMIT; SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_LogPetAction_Insert(
  IN p_PetNum INT, IN p_ItemMID INT, IN p_ItemSID INT, IN p_ActionType INT, IN p_PetFull INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  INSERT INTO LogPetAction (PetNum, ItemMID, ItemSID, ActionType, PetFull)
    VALUES (p_PetNum, p_ItemMID, p_ItemSID, p_ActionType, p_PetFull);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    COMMIT; SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_LogVehicleAction_Insert(
  IN p_VehicleNum INT, IN p_ItemMID INT, IN p_ItemSID INT, IN p_ActionType INT, IN p_VehicleBattery INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  INSERT INTO LogVehicleAction (VehicleNum, ItemMID, ItemSID, ActionType, VehicleBattery)
    VALUES (p_VehicleNum, p_ItemMID, p_ItemSID, p_ActionType, p_VehicleBattery);
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK; SET p_nReturn = -1;
  ELSE
    COMMIT; SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_logitemexchange_insert(
  IN p_NIDMain INT, IN p_NIDSub INT, IN p_SGNum INT, IN p_SvrNum INT, IN p_FldNum INT,
  IN p_MakeType INT, IN p_MakeNum DECIMAL(19,4), IN p_ItemAmount INT, IN p_ItemFromFlag INT, IN p_ItemFrom INT,
  IN p_ItemToFlag INT, IN p_ItemTo INT, IN p_ExchangeFlag INT, IN p_Damage INT, IN p_Defense INT,
  IN p_Fire INT, IN p_Ice INT, IN p_Poison INT, IN p_Electric INT, IN p_Spirit INT,
  IN p_CostumeMID INT, IN p_CostumeSID INT, IN p_TradePrice DECIMAL(19,4), OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  INSERT INTO LogItemExchange (NIDMain, NIDSub, SGNum, SvrNum, FldNum,
    MakeType, MakeNum, ItemAmount, ItemFromFlag, ItemFrom,
    ItemToFlag, ItemTo, ExchangeFlag, Damage, Defense,
    Fire, Ice, Poison, Electric, Spirit,
    CostumeMID, CostumeSID, TradePrice)
    VALUES (p_NIDMain, p_NIDSub, p_SGNum, p_SvrNum, p_FldNum,
    p_MakeType, p_MakeNum, p_ItemAmount, p_ItemFromFlag, p_ItemFrom,
    p_ItemToFlag, p_ItemTo, p_ExchangeFlag, p_Damage, p_Defense,
    p_Fire, p_Ice, p_Poison, p_Electric, p_Spirit,
    p_CostumeMID, p_CostumeSID, p_TradePrice);
  IF v_err <> 0 THEN SET p_nReturn = 0; ELSE SET p_nReturn = 1; END IF;
END$$

CREATE PROCEDURE sp_purchase_change_state(IN p_purkey VARCHAR(22), IN p_purflag INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0; DECLARE v_rc INT DEFAULT 0; DECLARE v_nFlag INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  -- (original had a dead post-SELECT error check on uninitialized vars; omitted)
  SELECT PurFlag INTO v_nFlag FROM ShopPurchase WHERE PurKey=p_purkey LIMIT 1;
  IF v_nFlag = p_purflag THEN
    SET p_nReturn = 0;
  ELSE
    UPDATE ShopPurchase SET PurFlag=p_purflag, PurChgDate=NOW() WHERE PurKey=p_purkey;
    SET v_rc = ROW_COUNT();
    IF v_err <> 0 OR v_rc = 0 THEN
      SET p_nReturn = 0;
    ELSE
      INSERT INTO LogShopPurchase (PurKey, PurFlag) VALUES (p_purkey, p_purflag);
      SET p_nReturn = 1;
    END IF;
  END IF;
END$$

DELIMITER ;
