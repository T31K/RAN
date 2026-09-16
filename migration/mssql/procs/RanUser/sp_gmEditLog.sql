
CREATE      PROCEDURE [dbo].[sp_gmEditLog]
	@nUserNum	int,
	@szGmCmd 	varChar(200),
	@userIP		varChar(50),
	@nReturn 	int	OUTPUT
AS

	DECLARE
		@error_var int, 
		@rowcount_var int

	SET NOCOUNT ON

	SET @nReturn = 0

	BEGIN TRAN

	INSERT INTO LogGmCmd( UserNum, GmCmd, UserIP ) Values ( @nUserNum, @szGmCmd, @userIP )

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
