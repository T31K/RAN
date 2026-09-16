-- RanGame1 stored procedures ported MSSQL T-SQL -> MariaDB (batch b)
-- Params prefixed p_ to avoid MariaDB case-insensitive collisions with column names.
USE RanGame1;

DROP PROCEDURE IF EXISTS DeleteChaFriend;
DROP PROCEDURE IF EXISTS sp_GetPetFull;
DROP PROCEDURE IF EXISTS sp_UpdateChaGender;
DROP PROCEDURE IF EXISTS UpdateChaFriend;
DROP PROCEDURE IF EXISTS sp_GetVehicleBattery;
DROP PROCEDURE IF EXISTS sp_update_guild_rank;
DROP PROCEDURE IF EXISTS sp_DeleteVehicle;
DROP PROCEDURE IF EXISTS sp_UpdatePetChaNum;
DROP PROCEDURE IF EXISTS UpdateChaLastCallPos;
DROP PROCEDURE IF EXISTS sp_RestorePet;
DROP PROCEDURE IF EXISTS sp_UpdatePetColor;
DROP PROCEDURE IF EXISTS sp_UpdatePetFull;
DROP PROCEDURE IF EXISTS sp_UpdatePetStyle;
DROP PROCEDURE IF EXISTS sp_UpdateVehicleBattery;

DELIMITER $$

CREATE PROCEDURE DeleteChaFriend(IN p_nChaP INT, IN p_nChaS INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  DELETE FROM ChaFriend WHERE ChaP=p_nChaP AND ChaS=p_nChaS;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_GetPetFull(IN p_nChaNum INT, IN p_nPetNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_found INT DEFAULT 1;
  DECLARE v_PetFull INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found = 0;
  SET p_nReturn = 0;
  SELECT PetFull INTO v_PetFull
  FROM PetInfo
  WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum AND PetDeleted=0
  LIMIT 1;
  IF v_err <> 0 OR v_found = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = v_PetFull;
  END IF;
END$$

CREATE PROCEDURE sp_UpdateChaGender(
  IN p_nChaNum INT, IN p_nChaClass INT, IN p_nChaSex INT, IN p_nFace INT,
  IN p_nChaHair INT, IN p_nChaHairColor INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_nChaNumTemp INT DEFAULT 0;
  SET p_nReturn = 0;
  SET v_nChaNumTemp = 0;
  IF EXISTS(SELECT ChaNum FROM ChaInfo WHERE ChaNum=p_nChaNum) THEN
    UPDATE ChaInfo SET ChaClass=p_nChaClass, ChaSex=p_nChaSex, ChaFace=p_nFace, ChaHair=p_nChaHair, ChaHairColor=p_nChaHairColor
    WHERE ChaNum=p_nChaNum;
    SET p_nReturn = 0;
  ELSE
    SET p_nReturn = -1;
  END IF;
END$$

CREATE PROCEDURE UpdateChaFriend(IN p_nChaP INT, IN p_nChaS INT, IN p_nFlag INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE ChaFriend
  SET ChaFlag=p_nFlag
  WHERE ChaP=p_nChaP AND ChaS=p_nChaS;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_GetVehicleBattery(IN p_nVehicleNum INT, IN p_nVehicleChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_found INT DEFAULT 1;
  DECLARE v_VehicleBattery INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_found = 0;
  SET p_nReturn = 0;
  SELECT VehicleBattery INTO v_VehicleBattery
  FROM VehicleInfo
  WHERE VehicleNum=p_nVehicleNum AND VehicleChaNum=p_nVehicleChaNum
  AND VehicleDeleted=0
  LIMIT 1;
  IF v_err <> 0 OR v_found = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = v_VehicleBattery;
  END IF;
END$$

CREATE PROCEDURE sp_update_guild_rank(IN p_nGuNum INT, IN p_nRank INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE GuildInfo
  SET GuRank=p_nRank
  WHERE GuNum=p_nGuNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_DeleteVehicle(IN p_nVehicleNum INT, IN p_nVehicleChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE VehicleInfo
  SET VehicleDeleted=1, VehicleDeletedDate=NOW()
  WHERE VehicleNum=p_nVehicleNum AND VehicleChaNum=p_nVehicleChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_UpdatePetChaNum(IN p_nPetChaNum INT, IN p_nPetNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo
  SET PetChaNum=p_nPetChaNum
  WHERE PetNum=p_nPetNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE UpdateChaLastCallPos(
  IN p_nChaReturnMap INT, IN p_fChaReturnPosX DOUBLE, IN p_fChaReturnPosY DOUBLE,
  IN p_fChaReturnPosZ DOUBLE, IN p_nChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  UPDATE ChaInfo SET
  ChaReturnMap=p_nChaReturnMap,
  ChaReturnPosX=p_fChaReturnPosX,
  ChaReturnPosY=p_fChaReturnPosY,
  ChaReturnPosZ=p_fChaReturnPosZ
  WHERE ChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    SET p_nReturn = -1;
  ELSE
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_RestorePet(IN p_nPetNum INT, IN p_nPetChaNum INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo SET PetDeleted=0, PetFull=1000, PetPutOnItems=''
  WHERE PetNum=p_nPetNum AND PetChaNum=p_nPetChaNum AND PetDeleted=1;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = p_nPetNum;
  END IF;
END$$

CREATE PROCEDURE sp_UpdatePetColor(IN p_nChaNum INT, IN p_nPetNum INT, IN p_nPetColor INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo
  SET PetColor=p_nPetColor
  WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_UpdatePetFull(IN p_nChaNum INT, IN p_nPetNum INT, IN p_nPetFull INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo
  SET PetFull=p_nPetFull
  WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_UpdatePetStyle(IN p_nChaNum INT, IN p_nPetNum INT, IN p_nPetStyle INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE PetInfo
  SET PetStyle=p_nPetStyle
  WHERE PetNum=p_nPetNum AND PetChaNum=p_nChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

CREATE PROCEDURE sp_UpdateVehicleBattery(
  IN p_nVehicleNum INT, IN p_nVehicleChaNum INT, IN p_nVehicleBattery INT, OUT p_nReturn INT)
BEGIN
  DECLARE v_err INT DEFAULT 0;
  DECLARE v_rc INT DEFAULT 0;
  DECLARE CONTINUE HANDLER FOR SQLEXCEPTION SET v_err = 1;
  SET p_nReturn = 0;
  START TRANSACTION;
  UPDATE VehicleInfo
  SET VehicleBattery=p_nVehicleBattery
  WHERE VehicleNum=p_nVehicleNum AND VehicleChaNum=p_nVehicleChaNum;
  SET v_rc = ROW_COUNT();
  IF v_err <> 0 OR v_rc = 0 THEN
    ROLLBACK;
    SET p_nReturn = -1;
  ELSE
    COMMIT;
    SET p_nReturn = 0;
  END IF;
END$$

DELIMITER ;
