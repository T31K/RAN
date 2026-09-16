

-- Return
-- DB_ERROR -1
-- Battery amount
CREATE PROCEDURE [dbo].[sp_GetVehicleBattery]
	@nVehicleNum	int,
	@nVehicleChaNum	int,
	@nReturn	int OUTPUT
AS
	DECLARE
		@error_var int,
		@rowcount_var int,
		@VehicleBattery int
	
	SET NOCOUNT ON
	
	SET @nReturn = 0

	SELECT @VehicleBattery = VehicleBattery
	FROM VehicleInfo
	WHERE VehicleNum=@nVehicleNum AND VehicleChaNum=@nVehicleChaNum
	AND VehicleDeleted=0	
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		SET @nReturn = @VehicleBattery
	END
	
	SET NOCOUNT OFF
	RETURN @nReturn


