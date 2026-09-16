
CREATE PROCEDURE [dbo].[UpdateChaNumDecrease]
	@nUserNum int,
	@nReturn int OUTPUT
AS	
	DECLARE
		@error_var int, 
		@rowcount_var int
		
	SET NOCOUNT ON
	
	SET @nReturn = 0
	
	UPDATE UserInfo
	SET ChaRemain=ChaRemain-1
	WHERE UserNum=@nUserNum
	
	SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
	IF @error_var <> 0 OR @rowcount_var = 0
	BEGIN
       	SET @nReturn = -1
		SET NOCOUNT OFF
		RETURN @nReturn
	END
	ELSE
	BEGIN
       	SET @nReturn = 0
		SET NOCOUNT OFF
		RETURN @nReturn
	END

