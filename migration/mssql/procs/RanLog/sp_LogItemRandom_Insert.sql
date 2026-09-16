

-- Return
-- DB_ERROR -1
-- DB_OK 0

CREATE Procedure [dbo].[sp_LogItemRandom_Insert]
	@NIDMain int,
	@NIDSub int,
	@SGNum int,
	@SvrNum int,
	@FldNum int,
	@MakeType int,
	@MakeNum money,
	@RandomType1 int, 
	@RandomValue1 int, 
	@RandomType2 int, 
	@RandomValue2 int, 
	@RandomType3 int, 
	@RandomValue3 int, 
	@RandomType4 int, 
	@RandomValue4 int,
	@nReturn int OUTPUT
AS
	DECLARE
		@error_var int, 
		@rowcount_var int
	SET NOCOUNT ON

	SET @nReturn = 0

	BEGIN TRAN

	Insert Into LogItemRandom (
		NIDMain, NIDSub, SGNum, SvrNum, FldNum, MakeType, MakeNum
		, RandomType1, RandomValue1, RandomType2, RandomValue2
		, RandomType3, RandomValue3, RandomType4, RandomValue4)
	Values (
		@NIDMain, @NIDSub, @SGNum, @SvrNum, @FldNum, @MakeType, @MakeNum
		, @RandomType1, @RandomValue1, @RandomType2, @RandomValue2
		, @RandomType3, @RandomValue3, @RandomType4, @RandomValue4
	)

	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
		ROLLBACK TRAN
		SET @nReturn = -1
	END
	ELSE
	BEGIN
		COMMIT TRAN
		SET @nReturn = 0
	END    

	SET NOCOUNT OFF

	RETURN @nReturn	


