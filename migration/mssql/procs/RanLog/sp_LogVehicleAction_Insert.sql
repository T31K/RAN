

-- Return
-- DB_ERROR -1
-- DB_OK 0

CREATE Procedure [dbo].[sp_LogVehicleAction_Insert]
	@VehicleNum int,
	@ItemMID int,
	@ItemSID int,
	@ActionType int,	
	@VehicleBattery int,
	@nReturn int OUTPUT
AS
	DECLARE
	@error_var int,
	@rowcount_var int
	SET NOCOUNT ON

	SET @nReturn=0

	BEGIN TRAN

	Insert Into LogVehicleAction( VehicleNum, ItemMID, ItemSID, ActionType, VehicleBattery )
	Values( @VehicleNum, @ItemMID, @ItemSID, @ActionType, @VehicleBattery )

	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
	IF @error_var<>0 OR @rowcount_var=0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn=-1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		SET @nReturn=0
	END

	SET NOCOUNT OFF

	RETURN @nReturn


