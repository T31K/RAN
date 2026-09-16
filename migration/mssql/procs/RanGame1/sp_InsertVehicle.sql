

-- Return (nReturn)
-- DB_ERROR -1
-- DB_OK 0

-- Return (nVehicleNum)

CREATE PROCEDURE [dbo].[sp_InsertVehicle]
	@szVehicleName	  varchar(20),
	@nVehicleChaNum	  int,
	@nVehicleType	  int,	
	@nVehicleCardMID  int,
	@nVehicleCardSID  int,
	@nReturn		  int OUTPUT
As
	DECLARE
		@error_var int, 
		@rowcount_var int

	SET NOCOUNT ON
	SET @nReturn = 0

	BEGIN TRAN

	INSERT INTO VehicleInfo (VehicleName, VehicleChaNum, VehicleType, VehicleCardMID, VehicleCardSID, VehiclePutOnItems)
	VALUES (@szVehicleName, @nVehicleChaNum, @nVehicleType, @nVehicleCardMID, @nVehicleCardSID, '')
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		UPDATE VehicleInfo SET VehicleNum=@@IDENTITY WHERE VehicleUniqueNum = @@IDENTITY
		SET @nReturn = @@IDENTITY
	END

	SET NOCOUNT OFF
	RETURN @nReturn


