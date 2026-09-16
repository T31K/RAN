
CREATE PROCEDURE [dbo].[UserLogoutSimple]
	@szUserID varchar(20)
AS
	SET NOCOUNT ON
	
	UPDATE UserInfo
	SET UserLoginState=0 
	WHERE UserID=@szUserID

	SET NOCOUNT OFF

