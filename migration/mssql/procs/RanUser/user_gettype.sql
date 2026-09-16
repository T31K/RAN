
CREATE PROCEDURE [dbo].[user_gettype]
	@nUserNum	int,
	@nReturn 	int	OUTPUT
AS	
    
	DECLARE @nUserType int

	SET NOCOUNT ON

	SET @nReturn = 0
	SET @nUserType = 0
	
	SELECT @nUserType = UserInfo.UserType 
	FROM UserInfo
	WHERE UserNum = @nUserNum
	
	SET @nReturn = @nUserType

	SET NOCOUNT OFF	
	
	RETURN @nReturn

