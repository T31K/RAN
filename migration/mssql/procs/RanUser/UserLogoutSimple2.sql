
CREATE PROCEDURE [dbo].[UserLogoutSimple2]
	@nUserNum int
AS
	SET NOCOUNT ON
	
	UPDATE UserInfo
	SET UserLoginState=0 
	WHERE UserNum=@nUserNum

	SET NOCOUNT OFF

